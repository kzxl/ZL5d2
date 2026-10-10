# ZeroLantern 5D2 ⚡📷

> **Dedicated Still Photography OS & Safe Rust Extension for Canon EOS 5D Mark II (DIGIC IV).**  
> Engineered by **ZeroUniverse** in collaboration with the open-source **Magic Lantern** community.

---

## 1. Overview & Philosophy

**ZeroLantern 5D2** is a streamlined, specialized fork of Magic Lantern engineered with single-minded focus:
* **Exclusively for Canon EOS 5D Mark II (Firmware v2.1.2)**: All source code, stubs, and build targets for other 30+ camera bodies have been completely stripped out to ensure an uncluttered, hyper-maintainable codebase.
* **Pure Still Photography Focus**: Stripped all heavy video recording subsystems (MLV RAW video, H.264 bitrate hacks, video filters) to maximize SDRAM buffer space, eliminate background task jitter, and provide zero shutter lag.
* **Hybrid C & Safe Rust Architecture**:
  - **C & ASM Core**: Manages the bootloader hijack (`src/boot-d45.c`), ARM946E-S context switches, and static ROM function pointers (`platform/5D2.212/stubs.S`).
  - **Rust `#![no_std]` Layer (`zero-d4`, `zero-photo`)**: Implements safe RAII synchronization, integer fixed-point arithmetic, RAW histogram analysis, and ND shutter calculation without risk of memory leaks or data races.

---

## 2. Architecture & Rust Workspace

```
ZeroLantern5D2/
├── platform/
│   └── 5D2.212/               # Sole supported camera platform
│       ├── consts.h           # DIGIC 4 MMIO & hardware registers
│       ├── features.h         # Still photography feature flags
│       └── stubs.S            # ROM entry points
├── src/                       # DIGIC 4 core runtime (C & ASM)
├── modules/                   # Still photography modules (dual_iso, ettr, bulb_nd, live_flt...)
└── rust/                      # Safe Rust (#![no_std]) Workspace
    ├── .cargo/config.toml     # armv5te-none-eabi bare-metal configuration
    └── crates/
        ├── zero-d4-sys        # Raw C ABI bindings for DryOS routines
        ├── zero-d4            # Safe HAL: RAII Mutex/Semaphores, Atomics, VRAM surfaces
        └── zero-photo         # Safe photographic math: Bulb ND, ETTR, Focus Peaking
```

---

## 3. Prioritized Capabilities for 5D Mark II

* **Dual ISO (`dual_iso.mo`)**: Dynamic range expansion up to **14.2+ stops** in a single `.CR2` RAW frame with zero motion ghosting.
* **Auto ETTR (`ettr.mo`)**: Sensor-level Expose-To-The-Right based directly on the 14-bit CMOS histogram.
* **Full-Resolution Silent Picture (`silent.mo`)**: Electronic shutter capture directly via EDMAC DMA with zero mechanical shutter wear.
* **Bulb ND Calculator (`bulb_nd.mo` & `zero-photo`)**: Quick optical ND preset selection and automatic long-exposure timing.
* **Electronic GND LiveView Filter (`live_flt.mo`)**: Real-time display gradient neutral density preview without dynamic memory allocation.

---

## 4. Legal, License & Attribution Notices

### 📜 Magic Lantern Base License (GNU GPL v2)
* This project incorporates and derives from the **Magic Lantern** firmware enhancement framework:
  - **Copyright (C) 2009–2026 Trammell Hudson & The Magic Lantern Team.**
  - Licensed under the **GNU General Public License Version 2 (GPL-2.0-only)** as published by the Free Software Foundation.
  - A complete copy of the GPL v2 license is provided in [`COPYING`](COPYING).

### 🦀 ZeroRust & Extension Crates License (MIT / GPL v2 Dual)
* The Rust crates located in `rust/crates/` (`zero-d4-sys`, `zero-d4`, `zero-photo`) are authored by **Phong Vo & The ZeroUniverse Team** and licensed under **MIT OR GPL-2.0-only**.
* The combined distributed binary (`autoexec.bin` and `.mo` modules) is subject to the terms of the **GNU GPL v2**.

### ⚖️ Trademark & Clean-Room Disclaimer
* "Canon", "EOS", "5D Mark II", "DryOS", and "DIGIC" are registered trademarks of **Canon Inc.**
* This project is an independent, non-commercial research and open-source compatibility project developed through clean-room interoperability methods. It is **not** endorsed by, affiliated with, or sponsored by Canon Inc.

---

## 5. Building

### Prerequisites
* ARM Embedded Toolchain: `arm-none-eabi-gcc`
* Rust Nightly (for bare-metal `armv5te-none-eabi` with `-Z build-std=core,compiler_builtins,alloc`)
* Python 3, Make

### Build Commands
```bash
# 1. Build Rust Static Library
cd rust
cargo build --release --target armv5te-none-eabi

# 2. Build 5D Mark II Firmware
cd ../platform/5D2.212
make

# 3. Build Still Photography Modules
cd ../../modules
make
```
