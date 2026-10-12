# rust/ — Experimental Rust Workspace (NOT linked)

> [!IMPORTANT]
> None of the crates in this workspace are compiled into `autoexec.bin` or any `.mo` module.
> No C code calls them. Histogram, ETTR and ND calculations are implemented in C
> (`modules/bulb_nd/bulb_nd.c`, `modules/ettr/ettr.c`).

The only Rust code that ships is the LiveView row filter in
[`modules/live_flt/rust_flt`](../modules/live_flt/rust_flt/src/lib.rs). It is built as one object by
`modules/live_flt/Makefile` (Cargo is not used for it).

## Crates

| Crate | Purpose | Status |
|---|---|---|
| `zero-d4-sys` | Raw DryOS FFI declarations and `struct task` mirror | Experimental. Layout checked at compile time against `src/tasks.h`. Some declared symbols (`_AllocateMemory`, `_alloc_dma_memory`, `qprintf`, `clean_d_cache`, `flush_caches`) are **not** exported by `magiclantern.sym`, so a module using them would fail to load. |
| `zero-d4` | Critical section, semaphore wrapper, VRAM surface | Experimental. Known open issues: the semaphore guard ignores `take_semaphore` failures (C4), the name is not required to be NUL-terminated (C5), and `VramSurface` creates `&mut [u8]` over DMA-written memory (C7). |
| `zero-photo` | Film simulation, ND and histogram helpers | Experimental. Film stock IDs 1..5 match the C side (`jpeg_enc.h`, `live_flt.film_sim`). |

## Shipping requirements

Before any crate is linked into a module, all of the following must hold:

1. Build it as a `staticlib` (or a single `--emit=obj`) for ARMv5TE with `-C relocation-model=static`.
2. The object must have **no undefined symbols** other than functions exported in
   `platform/5D2.212/build/magiclantern.sym`. Rust calls are not long calls, so any libcall into core can
   overflow the ±32 MB `BL` range. Use the gate in `modules/live_flt/Makefile` as the template.
3. Strip `.ARM.exidx*` instead of stubbing `__aeabi_unwind_cpp_pr0`. Keep only the FFI entry points global.
4. Fix the open issues listed above.
5. Zero heap: `alloc` is intentionally excluded from `build-std`.

## Checking

```powershell
cd rust
cargo check --offline --target armv5te-unknown-linux-musleabi --workspace
```

The `[unstable] build-std` section in `.cargo/config.toml` needs a nightly toolchain to target
`armv5te-none-eabi`. The command above uses the stable `armv5te-unknown-linux-musleabi` target for type
and layout checking only.
