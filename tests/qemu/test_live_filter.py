#!/usr/bin/env python3
"""
QEMU smoke test for the live_flt display filter (C module + Rust row filter object).

Proves on the emulated camera that live_flt.mo loads and that rust_flt_process_row runs:
  baseline   no filter            -> LiveView centre region carries colour (spread > threshold)
  trix       live_flt.film_sim=4  -> LiveView centre region is monochrome (Tri-X forces chroma to 128)

Usage: python tests/qemu/test_live_filter.py [--no-deploy]
Requires: make -C modules/live_flt (the build gate rejects undefined symbols in rust_flt.o).
"""
import os
import sys
import tempfile
import time

from PIL import Image, ImageChops

import qemu_harness as qh

MONO_MAX_SPREAD = 6     # JPEG-free screendump: allow for BMP overlay anti-aliasing at the crop edge
COLOUR_MIN_SPREAD = 20


def centre_spread(png_path):
    """Max |R-G| / |G-B| inside the central 50 % of the LCD (away from ML overlays)."""
    with Image.open(png_path) as img:
        w, h = img.size
        box = img.convert("RGB").crop((w // 4, h // 4, 3 * w // 4, 3 * h // 4))
        r, g, b = box.split()
        return max(ImageChops.difference(r, g).getextrema()[1], ImageChops.difference(g, b).getextrema()[1])


def liveview_spread(filter_cfg, shot_name, out_dir):
    qh.reset_card({"LIVE_FLT.CFG": ["# Config file for module live_flt (LIVE_FLT.MO)"] + filter_cfg})
    shot = os.path.join(out_dir, shot_name)
    with qh.Camera() as cam:
        cam.key("l", wait=8.0)  # LiveView (qemu-eos: L); display filters run only in LiveView
        cam.screenshot(shot)
    return centre_spread(shot) if os.path.exists(shot) else None


def main(argv):
    qh.kill_qemu()
    if "--no-deploy" not in argv:
        qh.deploy_modules("live_flt")
    out_dir = tempfile.mkdtemp(prefix="live_flt_")
    baseline = liveview_spread(["live_flt.film_sim = 0"], "baseline.png", out_dir)
    trix = liveview_spread(["live_flt.film_sim = 4"], "trix.png", out_dir)
    print(f"screenshots: {out_dir}")
    print(f"baseline centre spread = {baseline}, trix centre spread = {trix}")
    checks = [
        ("baseline LiveView has colour (check is not vacuous)", baseline is not None and baseline >= COLOUR_MIN_SPREAD),
        ("Tri-X filter output is monochrome (Rust row filter ran)", trix is not None and trix <= MONO_MAX_SPREAD),
    ]
    failures = 0
    for label, ok in checks:
        print(f"{'PASS' if ok else 'FAIL'} live_flt: {label}")
        failures += 0 if ok else 1
    print(f"\n{'ALL PASSED' if failures == 0 else f'{failures} check(s) FAILED'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
