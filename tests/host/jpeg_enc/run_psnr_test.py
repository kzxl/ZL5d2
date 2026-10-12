#!/usr/bin/env python3
"""
Host reproduction test for modules/silent/jpeg_enc.c.

Builds the production encoder with the host compiler, encodes synthetic UYVY
frames containing real AC energy (gradients, edges, text-like detail), decodes
the result with Pillow, and asserts a minimum luma PSNR against the source.

Flat single-colour frames (as produced by QEMU LiveView) only exercise the DC
coefficient and therefore cannot detect AC ordering or scaling defects.
"""
import os
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
SRC = os.environ.get("JPEG_ENC_SRC", os.path.join(REPO, "modules", "silent", "jpeg_enc.c"))
INC = os.path.join(REPO, "modules", "silent")
STUBS = os.path.join(HERE, "stubs")
DRIVER = os.path.join(HERE, "jpeg_enc_test.c")

WIDTH, HEIGHT = 720, 480
QUALITIES = (70, 85, 95)
# Maximum allowed PSNR deficit versus libjpeg (Pillow) encoding the same luma at the same quality.
ORACLE_TOLERANCE_DB = 1.5


def oracle_psnr(luma, quality):
    import io

    buf = io.BytesIO()
    Image.fromarray(luma).save(buf, "JPEG", quality=quality)
    buf.seek(0)
    with Image.open(buf) as img:
        return psnr(luma, np.asarray(img.convert("L")))


def build(out_dir):
    exe = os.path.join(out_dir, "jpeg_enc_test.exe")
    cmd = ["gcc", "-std=gnu99", "-O1", "-w", "-I", STUBS, "-I", INC, DRIVER, SRC, "-o", exe]
    subprocess.run(cmd, check=True)
    return exe


def make_frames():
    y, x = np.mgrid[0:HEIGHT, 0:WIDTH].astype(np.float64)
    frames = {}
    frames["gradient"] = (x / WIDTH) * 255.0
    frames["checker8"] = (((x // 8) + (y // 8)) % 2) * 200.0 + 28.0
    frames["sine"] = 128.0 + 100.0 * np.sin(x / 6.0) * np.cos(y / 9.0)
    rng = np.random.default_rng(1234)
    frames["noise_smooth"] = np.clip(
        128.0 + 60.0 * np.sin(x / 23.0) + rng.normal(0.0, 12.0, (HEIGHT, WIDTH)), 0, 255
    )
    return {k: np.clip(v, 0, 255).astype(np.uint8) for k, v in frames.items()}


def to_uyvy(luma):
    uyvy = np.empty((HEIGHT, WIDTH * 2), dtype=np.uint8)
    uyvy[:, 0::4] = 128  # U
    uyvy[:, 1::4] = luma[:, 0::2]  # Y0
    uyvy[:, 2::4] = 128  # V
    uyvy[:, 3::4] = luma[:, 1::2]  # Y1
    return uyvy


def psnr(a, b):
    mse = np.mean((a.astype(np.float64) - b.astype(np.float64)) ** 2)
    return float("inf") if mse == 0 else 10.0 * np.log10(255.0 ** 2 / mse)


def main():
    failures = 0
    total = 0
    with tempfile.TemporaryDirectory() as tmp:
        exe = build(tmp)
        for name, luma in make_frames().items():
            raw_path = os.path.join(tmp, f"{name}.uyvy")
            to_uyvy(luma).tofile(raw_path)
            for quality in QUALITIES:
                total += 1
                threshold = oracle_psnr(luma, quality) - ORACLE_TOLERANCE_DB
                jpg = os.path.join(tmp, f"{name}_q{quality}.jpg")
                rc = subprocess.run([exe, raw_path, str(WIDTH), str(HEIGHT), str(quality), jpg]).returncode
                if rc != 0:
                    print(f"FAIL {name} q{quality}: encoder rc={rc}")
                    failures += 1
                    continue
                try:
                    with Image.open(jpg) as img:
                        decoded = np.asarray(img.convert("L"))
                except Exception as exc:  # decoder rejected the bitstream
                    print(f"FAIL {name} q{quality}: decode error {exc}")
                    failures += 1
                    continue
                score = psnr(luma, decoded)
                status = "PASS" if score >= threshold else "FAIL"
                if status == "FAIL":
                    failures += 1
                print(f"{status} {name:13s} q{quality}: PSNR {score:6.2f} dB (min {threshold:6.2f} = libjpeg - {ORACLE_TOLERANCE_DB})")
    print(f"\n{total - failures}/{total} assertions passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
