#!/usr/bin/env python3
"""
Strict QEMU concurrency tests for the silent module (exact capture counts).

Scenarios (default: menu_bracket, rapid_snap; name scenarios on the command line to override):
  menu_bracket       Bracket Snap Now with 3 frames configured  -> exactly 3 JPEGs, all decodable
  rapid_snap         three SET presses within ~150 ms           -> exactly 1 JPEG
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

from PIL import Image

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
    return report("rapid_snap", qh.list_captures(), 1,
                  [("single snap task started",
                    lambda log: sum("silent_snap_task started" in l for l in log) == 1)])


def scenario_halfshutter_hold(args):
    qh.reset_card({"SILENT.CFG": SILENT_BASE + BRACKET_3})
    with qh.Camera() as cam:
        cam.key("l", wait=5.0)  # LiveView (qemu-eos: L), menu closed; polling CBR requires lv
        cam.key("shift", hold_ms=args.hold_ms, wait=0.2)  # half-shutter held while the bracket starts
        time.sleep(args.settle_s)
    return report("halfshutter_hold", qh.list_captures(), 3,
                  [("no self-abort while button still held",
                    lambda log: not any("aborted by user" in l for l in log))])


DEFAULT_SCENARIOS = ["menu_bracket", "rapid_snap"]

SCENARIOS = {
    "menu_bracket": scenario_menu_bracket,
    "rapid_snap": scenario_rapid_snap,
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
