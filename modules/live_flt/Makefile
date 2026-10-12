# define the module name - make sure name is max 8 characters
MODULE_NAME = live_flt
MODULE_OBJS = $(BUILD_DIR)/live_flt.o $(BUILD_DIR)/rust_flt.o
TOP_DIR = ../..

# include modules environment
include $(TOP_DIR)/modules/Makefile
.DEFAULT_GOAL := $(BUILD_DIR)/module_complete

RUST_NM = $(CROSS_COMPILE)nm

# Rust row filter, compiled as a single object (Cargo.toml is not used by this rule).
# - relocation-model=static: the Linux target defaults to PIC; the ML module loader expects plain relocations.
# - target-cpu=arm946e-s:    DIGIC 4 core (ARMv5TE, no hardware divider).
# - .ARM.exidx* are stripped: panic=abort never unwinds, and they pull in __aeabi_unwind_cpp_pr0.
# - Only the two FFI entry points stay global, so nothing else leaks into the module symbol namespace.
# - The object must have no undefined symbols: Rust calls are not long calls, so any libcall into
#   core could overflow the +-32 MB BL range once the module loads far from core.
$(BUILD_DIR)/rust_flt.o: rust_flt/src/lib.rs | $(BUILD_DIR)
	rustc --edition 2021 --target armv5te-unknown-linux-musleabi --crate-type=staticlib --emit=obj \
		-C panic=abort -C force-unwind-tables=no -C opt-level=3 \
		-C relocation-model=static -C target-cpu=arm946e-s $< -o $@.raw
	$(OBJCOPY) --remove-section=.ARM.exidx* --remove-section=.rel.ARM.exidx* \
		--strip-symbol=__aeabi_unwind_cpp_pr0 \
		--keep-global-symbol=rust_flt_process_row --keep-global-symbol=rust_flt_get_crop_bounds \
		$@.raw $@
	@if [ -n "$$($(RUST_NM) -u $@)" ]; then echo "rust_flt.o has undefined symbols:"; $(RUST_NM) -u $@; rm -f $@; exit 1; fi
	@rm -f $@.raw

# Host unit tests for the Rust filter (std build, debug overflow checks enabled).
rust_flt_test: | $(BUILD_DIR)
	rustc --edition 2021 --test rust_flt/src/lib.rs -o $(BUILD_DIR)/rust_flt_test$(EXE_EXT)
	$(BUILD_DIR)/rust_flt_test$(EXE_EXT)

.PHONY: rust_flt_test
