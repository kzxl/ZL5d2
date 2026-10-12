#!/usr/bin/env python3
"""
Minimal QEMU-EOS harness for 5D2 module tests (runs QEMU and mtools inside WSL).

Environment overrides:
  QEMU_EOS_BIN   path to qemu-system-arm inside WSL
  QEMU_5D2_DIR   working directory holding ROMs and the CF image (WSL path)
  QEMU_CF_IMAGE  CF image file name inside QEMU_5D2_DIR

Key mapping (qemu-eos hw/eos/mpu.c): Shift = half-shutter, Space = SET,
Delete = MENU/ML menu, Q = LiveView, W = Q button (opens submenu).
"""
import os
import socket
import subprocess
import time

QEMU_BIN = os.environ.get("QEMU_EOS_BIN", "/mnt/e/Tools/qemu-eos-build/arm-softmmu/qemu-system-arm")
WORK_DIR = os.environ.get("QEMU_5D2_DIR", "/mnt/e/Tools/qemu_5d2")
CF_IMAGE = f"{WORK_DIR}/{os.environ.get('QEMU_CF_IMAGE', 'cf_ml5d2.raw')}"
CF = f"{CF_IMAGE}@@50688"  # FAT partition offset inside the raw image
MONITOR = ("127.0.0.1", 4444)
DCIM = "::/DCIM/100EOS5D/"
REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def wsl(*args, text=True):
    return subprocess.run(["wsl", *args], capture_output=True, text=text)


def to_wsl_path(win_path):
    drive, rest = os.path.splitdrive(os.path.abspath(win_path))
    return f"/mnt/{drive[0].lower()}{rest.replace(os.sep, '/')}"


def kill_qemu():
    wsl("pkill", "-9", "-f", "qemu-system-arm")
    time.sleep(1)


def deploy_modules(*names):
    """Copies freshly built modules/<name>/build/<name>.mo onto the CF image."""
    for name in names:
        src = to_wsl_path(os.path.join(REPO, "modules", name, "build", f"{name}.mo"))
        res = wsl("mcopy", "-o", "-i", CF, src, f"::/ML/MODULES/{name.upper()}.MO")
        if res.returncode != 0:
            raise RuntimeError(f"deploy {name}: {res.stderr.strip()}")


def reset_card(module_configs):
    """Removes captures, logs and settings, then writes the given module configs."""
    for path in ("::/ML/MODULES/LOADING.LCK", "::/ML/SETTINGS/MAGIC.CFG", "::/ML/SETTINGS/MENUS.CFG",
                 "::/SILENT.LOG", f"{DCIM}*.JPG", f"{DCIM}*.DNG"):
        wsl("mdel", "-i", CF, path)
    for file_name, lines in module_configs.items():
        body = "\\n".join(lines) + "\\n"  # printf expands \n; avoids raw newlines on the Windows command line
        tmp = f"/tmp/{file_name.lower()}"
        wsl("bash", "-c", f"printf '{body}' > {tmp} && mcopy -o -i {CF} {tmp} ::/ML/SETTINGS/{file_name}")


def list_captures(ext=".JPG"):
    res = wsl("mdir", "-b", "-i", CF, DCIM)
    return sorted(os.path.basename(l.strip()) for l in res.stdout.splitlines() if l.strip().upper().endswith(ext))


def read_card_file(card_path):
    res = wsl("mtype", "-i", CF, card_path)
    return res.stdout if res.returncode == 0 else ""


def fetch_capture(name, dest_dir):
    dest = os.path.join(dest_dir, name)
    wsl("mcopy", "-o", "-i", CF, f"{DCIM}{name}", to_wsl_path(dest))
    return dest


class Camera:
    """Boots QEMU and drives the HMP monitor."""

    def __init__(self, boot_seconds=12):
        self.boot_seconds = boot_seconds
        self.proc = None
        self.sock = None

    def __enter__(self):
        kill_qemu()
        cmd = ["wsl", "--cd", WORK_DIR, QEMU_BIN, "-name", "5D2", "-M", "5D2,firmware=boot=1",
               "-drive", f"if=ide,format=raw,file={CF_IMAGE}",
               "-monitor", f"telnet:{MONITOR[0]}:{MONITOR[1]},server,nowait",
               "-serial", f"file:{WORK_DIR}/serial.log", "-display", "none"]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(30):
            time.sleep(0.5)
            try:
                self.sock = socket.create_connection(MONITOR, timeout=2.0)
                break
            except OSError:
                continue
        if not self.sock:
            raise RuntimeError("QEMU monitor did not come up")
        time.sleep(self.boot_seconds)
        return self

    def __exit__(self, *exc):
        try:
            if self.sock:
                self.sock.close()
        finally:
            if self.proc:
                self.proc.kill()
            kill_qemu()

    def key(self, name, hold_ms=None, wait=0.3):
        cmd = f"sendkey {name}" if hold_ms is None else f"sendkey {name} {hold_ms}"
        self.sock.sendall(f"{cmd}\n".encode())
        time.sleep(wait)
        try:
            self.sock.recv(4096)
        except OSError:
            pass

    def screenshot(self, win_png_path):
        """Saves the current LCD as PNG (via a PPM screendump in the WSL work dir)."""
        ppm = f"{WORK_DIR}/_shot.ppm"
        self.sock.sendall(f"screendump {ppm}\n".encode())
        time.sleep(0.8)
        try:
            self.sock.recv(4096)
        except OSError:
            pass
        wsl("cp", ppm, to_wsl_path(win_png_path + ".ppm"))
        try:
            from PIL import Image
            with Image.open(win_png_path + ".ppm") as img:
                img.convert("RGB").save(win_png_path)
            os.remove(win_png_path + ".ppm")
        except Exception as exc:
            print(f"    screenshot failed: {exc}")

    def open_silent_submenu(self):
        """LiveView -> ML menu -> Capture tab -> Silent Picture -> submenu (cursor on Snap Now)."""
        self.key("q", wait=1.5)
        self.key("delete", wait=2.0)
        self.key("spc", wait=0.8)
        self.key("up")
        for _ in range(3):
            self.key("right")
        time.sleep(0.3)
        for _ in range(5):
            self.key("down", wait=0.2)
        self.key("w", wait=0.8)
