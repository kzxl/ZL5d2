# rust/ — ZeroRust Ecosystem Integration

> [!NOTE]
> All Rust computational photography algorithms (`zero-photo`) and camera Hardware Abstraction Layer
> primitives (`zero-d4`) have been consolidated into the centralized monorepo:
> **[`ZeroUniverse / ZeroRust`](https://github.com/kzxl/ZeroRust)**.

## Architecture

- **`ZeroLantern5D2` (Pure C Embedded Firmware & Module Runtime)**:
  - Serves as the consumer/runtime for Canon 5D Mark II.
  - Links pre-compiled objects (`rust_flt.o`) generated from the `zero-photo` engine.
  - Does not maintain fragmented or duplicate Rust implementations.

- **`ZeroUniverse / ZeroRust` (Algorithmic & Color Science Source of Truth)**:
  - `crates/zero-photo`: Fujifilm-style film simulations (Portra, Velvia, Tri-X, Cinestill, Classic Chrome), tone curves, RAW histogram percentiles, optical ND calculations, and the real-time LiveView row grading pipeline (`rust_flt_process_row`, `rust_flt_get_crop_bounds`).
  - `crates/zero-d4`: Safe DryOS RTOS abstractions, `struct Task` layout validation (0x50 bytes), single-core ARMv5TE memory barriers, and RAII semaphores.

## Building `live_flt.mo` with ZeroRust

If `ZeroRust` is present at `E:/15. Other/ZeroUniverse/ZeroRust`, `modules/live_flt/Makefile` automatically resolves the source:
```bash
make -C modules/live_flt ARM_BINPATH=E:/Tools/arm-gnu-toolchain/bin
```
The build rule uses:
- `--edition 2021 --target armv5te-unknown-linux-musleabi`
- `-C relocation-model=static -C target-cpu=arm946e-s -C panic=abort`
- Strips `.ARM.exidx*` and enforces a strict zero-undefined-symbol policy.
