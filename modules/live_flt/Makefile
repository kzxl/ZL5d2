# define the module name - make sure name is max 8 characters
MODULE_NAME = live_flt
MODULE_OBJS = $(BUILD_DIR)/live_flt.o $(BUILD_DIR)/rust_flt.o
TOP_DIR = ../..

# include modules environment
include $(TOP_DIR)/modules/Makefile
.DEFAULT_GOAL := $(BUILD_DIR)/module_complete

$(BUILD_DIR)/rust_flt.o: rust_flt/src/lib.rs | $(BUILD_DIR)
	rustc --target armv5te-unknown-linux-musleabi --crate-type=staticlib --emit=obj -O -C panic=abort -C force-unwind-tables=no -C opt-level=3 $< -o $@
