#!/usr/bin/env python3
"""
Strict QEMU concurrency tests for the silent module (exact capture counts).

Scenarios (default: menu_bracket, rapid_snap, recipe_snap, multiexp_recipe; name scenarios to override):
  menu_bracket       Bracket Snap Now with 3 frames configured  -> exactly 3 JPEGs, all decodable
  rapid_snap         three SET presses within ~150 ms           -> exactly 1 JPEG
  recipe_snap        film recipe active, single snap            -> exactly 1 JPEG (encoder strip grading, S7)
  multiexp_recipe    2-frame multi-exposure + film recipe       -> exactly 1 JPEG, accumulator allocated (S6)
  halfshutter_hold   bracket started by holding half-shutter    -> exactly 3 JPEGs, no self-abort (S1)
                     OPT-IN ONLY: not reproducible in qemu-eos 5D2. Measured: with a 6 s Shift hold the
                     firmware flag at 0x1c10 reads 1 for ~330 ms only, and display_idle() stays 0 in
                     LiveView, so the polling CBR's 500 ms long-press gate is never satisfied.

Usage: python tests/qemu/test_capture_concurrency.py [scenario ...] [--hold-ms N] [--settle-s S] [--no-deploy]
Requires the modules to be built first (make -C modules/silent, make -C modules/live_flt).
"""
import os
import sys
import tempfile
import time

from PIL import Image, ImageChops

import qemu_harness as qh

SILENT_BASE = [
    "# Config file for module silent (SILENT.MO)",
    "silent.pic = 1",
    "silent.pic.file_format = 3",  # JPEG
    "silent.pic.film_sim = 0",
    "silent.pic.quality = 1",
]
BRACKET_3 = ["silent.bracket_steps = 1", "silent.bracket_stepsize = 0"]  # 3 frames, 1 EV


def silent_log_lines():
    return [l.strip() for l in qh.read_card_file("::/SILENT.LOG").splitlines() if l.strip()]


def decodable(names):
    ok = 0
    with tempfile.TemporaryDirectory() as tmp:
        for name in names:
            try:
                with Image.open(qh.fetch_capture(name, tmp)) as img:
                    img.load()
                ok += 1
            except Exception as exc:  # corrupt / truncated JPEG
                print(f"    decode failed for {name}: {exc}")
    return ok


def report(name, captures, expected, extra_checks=()):
    log = silent_log_lines()
    print(f"[{name}] captures={len(captures)} expected={expected} -> {captures}")
    for line in log[-12:]:
        print(f"    log: {line}")
    checks = [(f"exactly {expected} JPEG(s)", len(captures) == expected),
              ("all captures decode", decodable(captures) == len(captures))]
    checks.extend((label, fn(log)) for label, fn in extra_checks)
    failures = 0
    for label, ok in checks:
        print(f"{'PASS' if ok else 'FAIL'} {name}: {label}")
        failures += 0 if ok else 1
    return failures


def scenario_menu_bracket(args):
    qh.reset_card({"SILENT.CFG": SILENT_BASE + BRACKET_3})
    with qh.Camera() as cam:
        cam.open_silent_submenu()
        cam.key("spc")  # Snap Now -> bracket (bracket_steps > 0)
        time.sleep(args.settle_s)
    return report("menu_bracket", qh.list_captures(), 3,
                  [("no abort logged", lambda log: not any("aborted by user" in l for l in log)),
                   ("snap task finished", lambda log: any("silent_snap_task finished" in l for l in log))])


def scenario_rapid_snap(_args):
    qh.reset_card({"SILENT.CFG": SILENT_BASE + ["silent.bracket_steps = 0"]})
    with qh.Camera() as cam:
        cam.open_silent_submenu()
        for _ in range(3):
            cam.key("spc", wait=0.05)
        time.sleep(10)
    captures = qh.list_captures()
    spread = chroma_spread(captures[0]) if captures else 0
    print(f"    rapid_snap (ungraded baseline) chroma spread = {spread}")
    return report("rapid_snap", captures, 1,
                  [("single snap task started",
                    lambda log: sum("silent_snap_task started" in l for l in log) == 1),
                   ("ungraded capture has colour (monochrome checks are not vacuous)", lambda _log: spread > 3)])


def scenario_halfshutter_hold(args):
    qh.reset_card({"SILENT.CFG": SILENT_BASE + BRACKET_3})
    with qh.Camera() as cam:
        cam.key("l", wait=5.0)  # LiveView (qemu-eos: L), menu closed; polling CBR requires lv
        cam.key("shift", hold_ms=args.hold_ms, wait=0.2)  # half-shutter held while the bracket starts
        time.sleep(args.settle_s)
    return report("halfshutter_hold", qh.list_captures(), 3,
                  [("no self-abort while button still held",
                    lambda log: not any("aborted by user" in l for l in log))])


TRIX_RECIPE = [l for l in SILENT_BASE if "film_sim" not in l] + [
    "silent.pic.film_sim = 5",  # film_sim 5 -> stock 4 (Tri-X, chroma forced to 128)
    "silent.pic.grain = 0",
]


def chroma_spread(name):
    """Max per-pixel |R-G| / |G-B| of a capture; ~0 for a monochrome JPEG."""
    with tempfile.TemporaryDirectory() as tmp:
        with Image.open(qh.fetch_capture(name, tmp)) as img:
            r, g, b = img.convert("RGB").split()
            rg = ImageChops.difference(r, g).getextrema()[1]
            gb = ImageChops.difference(g, b).getextrema()[1]
    return max(rg, gb)


def scenario_recipe_snap(_args):
    """S7: film recipe on a live-VRAM source goes through the encoder strip copy."""
    qh.reset_card({"SILENT.CFG": TRIX_RECIPE + ["silent.bracket_steps = 0"]})
    with qh.Camera() as cam:
        cam.open_silent_submenu()
        cam.key("spc")
        time.sleep(15)
    captures = qh.list_captures()
    spread = chroma_spread(captures[0]) if captures else 255
    print(f"    recipe_snap chroma spread = {spread}")
    return report("recipe_snap", captures, 1,
                  [("encoder reported success", lambda log: any("returned SUCCESS" in l for l in log)),
                   ("recipe active (stock 4)", lambda log: any("jpeg: recipe stock=4" in l for l in log)),
                   ("saved file is monochrome (recipe applied)", lambda _log: spread <= 3)])


def scenario_multiexp_recipe(_args):
    """S6 + S7: 2-frame multi-exposure accumulator, graded by the encoder, freed after encode."""
    qh.reset_card({"SILENT.CFG": TRIX_RECIPE + ["silent.bracket_steps = 0", "silent.multiexp = 1"]})
    with qh.Camera() as cam:
        cam.open_silent_submenu()
        cam.key("spc")
        time.sleep(20)
    captures = qh.list_captures()
    spread = chroma_spread(captures[0]) if captures else 255
    print(f"    multiexp_recipe chroma spread = {spread}")
    return report("multiexp_recipe", captures, 1,
                  [("encoder reported success", lambda log: any("returned SUCCESS" in l for l in log)),
                   ("2 of 2 frames blended", lambda log: any("multiexp: blended 2/2 frames" in l for l in log)),
                   ("no allocation failure", lambda log: not any("allocation failed" in l for l in log)),
                   ("saved file is monochrome (recipe applied)", lambda _log: spread <= 3)])


DEFAULT_SCENARIOS = ["menu_bracket", "rapid_snap", "recipe_snap", "multiexp_recipe"]

SCENARIOS = {
    "menu_bracket": scenario_menu_bracket,
    "rapid_snap": scenario_rapid_snap,
    "recipe_snap": scenario_recipe_snap,
    "multiexp_recipe": scenario_multiexp_recipe,
    "halfshutter_hold": scenario_halfshutter_hold,
}


class Args:
    def __init__(self, argv):
        self.hold_ms = 2500
        self.settle_s = 30
        self.names = []
        it = iter(argv)
        for a in it:
            if a == "--hold-ms":
                self.hold_ms = int(next(it))
            elif a == "--settle-s":
                self.settle_s = float(next(it))
            elif a == "--no-deploy":
                self.deploy = False
            else:
                self.names.append(a)
        self.deploy = getattr(self, "deploy", True)


def main(argv):
    args = Args(argv)
    names = args.names or DEFAULT_SCENARIOS
    unknown = [n for n in names if n not in SCENARIOS]
    if unknown:
        print(f"unknown scenario(s): {unknown}; choose from {list(SCENARIOS)}")
        return 2
    qh.kill_qemu()
    if args.deploy:
        qh.deploy_modules("silent", "live_flt")
    failures = 0
    for name in names:
        print(f"\n=== {name} ===")
        failures += SCENARIOS[name](args)
    print(f"\n{'ALL PASSED' if failures == 0 else f'{failures} check(s) FAILED'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
