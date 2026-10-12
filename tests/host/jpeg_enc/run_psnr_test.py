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
NEGATIVE_DRIVER = os.path.join(HERE, "negative_test.c")
HOST_FIO = os.path.join(STUBS, "host_fio.c")
# Undefined behaviour (e.g. shift by >= 32, signed overflow) aborts the test binary instead of passing silently.
SANITIZE = ["-fsanitize=undefined", "-fsanitize-undefined-trap-on-error"]

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


def build(out_dir, driver=DRIVER, name="jpeg_enc_test.exe"):
    exe = os.path.join(out_dir, name)
    cmd = ["gcc", "-std=gnu99", "-O1", "-w", *SANITIZE, "-I", STUBS, "-I", INC, driver, HOST_FIO, SRC, "-o", exe]
    subprocess.run(cmd, check=True)
    return exe


def run_negative_tests(tmp):
    """Builds and runs negative_test.c, then verifies its EXIF outputs; returns the failure count."""
    exe = build(tmp, NEGATIVE_DRIVER, "negative_test.exe")
    out_dir = os.path.join(tmp, "negative")
    os.makedirs(out_dir, exist_ok=True)
    rc = subprocess.run([exe, out_dir]).returncode
    if rc < 0 or rc > 255:
        print(f"FAIL negative_test terminated abnormally (rc={rc}; UBSan trap or crash)")
        rc = 1
    failures = rc
    failures += verify_exif_file(os.path.join(out_dir, "nominal_exif.jpg"),
                                 model="Canon EOS 5D Mark II", iso=400, film="Portra 400")
    failures += verify_exif_file(os.path.join(out_dir, "long_exif.jpg"),
                                 model="M" * EXIF_MODEL_MAX_CHARS, iso=100, film=None)
    return failures


EXIF_MODEL_MAX_CHARS = 63
TIFF_TYPE_SIZE = {1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 7: 1}


def _segments(data):
    """Yields (marker, payload) for every marker segment before SOS."""
    pos = 2
    while pos + 4 <= len(data):
        if data[pos] != 0xFF:
            raise ValueError(f"expected marker at {pos}")
        marker = data[pos + 1]
        length = int.from_bytes(data[pos + 2:pos + 4], "big")
        if pos + 2 + length > len(data):
            raise ValueError(f"segment 0x{marker:02X} overruns file")
        yield marker, data[pos + 4:pos + 2 + length]
        if marker == 0xDA:
            return
        pos += 2 + length


def _ifd_offsets_in_bounds(tiff, ifd_offset):
    """Returns (ok, sub_ifd_offset) after checking every out-of-line value lies inside the TIFF block."""
    if ifd_offset + 2 > len(tiff):
        return False, None
    count = int.from_bytes(tiff[ifd_offset:ifd_offset + 2], "little")
    sub_ifd = None
    for i in range(count):
        entry = ifd_offset + 2 + i * 12
        if entry + 12 > len(tiff):
            return False, None
        tag = int.from_bytes(tiff[entry:entry + 2], "little")
        typ = int.from_bytes(tiff[entry + 2:entry + 4], "little")
        cnt = int.from_bytes(tiff[entry + 4:entry + 8], "little")
        val = int.from_bytes(tiff[entry + 8:entry + 12], "little")
        size = TIFF_TYPE_SIZE.get(typ, 1) * cnt
        if size > 4 and val + size > len(tiff):
            return False, None
        if tag == 0x8769:
            sub_ifd = val
    return True, sub_ifd


def verify_exif_file(path, model, iso, film):
    """Checks marker order, IFD bounds and tag values of an encoder output with EXIF; returns failure count."""
    name = os.path.basename(path)
    checks = []
    try:
        with open(path, "rb") as fh:
            data = fh.read()
        segs = list(_segments(data))
        markers = [m for m, _ in segs]
        checks.append(("APP1 directly after SOI", markers[:1] == [0xE1]))
        checks.append(("no APP0 when EXIF present", 0xE0 not in markers))
        tiff = segs[0][1][6:]
        ok0, sub = _ifd_offsets_in_bounds(tiff, int.from_bytes(tiff[4:8], "little"))
        ok1 = sub is not None and _ifd_offsets_in_bounds(tiff, sub)[0]
        checks.append(("IFD values within APP1 payload", ok0 and ok1))
        with Image.open(path) as img:
            img.load()
            exif = img.getexif()
            sub_ifd = exif.get_ifd(0x8769)
        checks.append(("Make == Canon", exif.get(0x010F) == "Canon"))
        checks.append((f"Model == {model[:24]}...", exif.get(0x0110) == model))
        checks.append((f"ISO == {iso}", sub_ifd.get(0x8827) == iso))
        if film:
            comment = sub_ifd.get(0x9286, b"")
            if isinstance(comment, str):
                comment = comment.encode("latin-1")
            checks.append(("UserComment carries film name", film.encode() in comment))
    except Exception as exc:
        checks.append((f"parse ({exc})", False))
    failures = 0
    for label, ok in checks:
        print(f"{'PASS' if ok else 'FAIL'} {name}: {label}")
        failures += 0 if ok else 1
    return failures


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
        print(f"\nPSNR: {total - failures}/{total} assertions passed\n")
        negative_failures = run_negative_tests(tmp)
    return 1 if (failures or negative_failures) else 0


if __name__ == "__main__":
    sys.exit(main())
