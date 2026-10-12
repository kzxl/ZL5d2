//! Raw low-level FFI stubs for DIGIC 4 / DryOS RTOS.
//!
//! Maps directly to static addresses and C ABI functions defined in
//! `platform/5D2.212/stubs.S` and `src/dryos.h`.

#![no_std]

use core::ffi::c_void;

pub type TaskEntry = extern "C" fn(arg: *mut c_void);

#[repr(C)]
pub struct Semaphore {
    _private: [u8; 0],
}

/// DryOS task control block, mirrors `struct task` in `src/tasks.h` (CONFIG_TASK_STRUCT_V1, size 0x50).
#[repr(C)]
pub struct Task {
    pub prev_task: *mut Task,     // 0x00
    pub next_task: *mut Task,     // 0x04
    pub run_prio: u32,            // 0x08
    pub entry: *mut c_void,       // 0x0c
    pub arg: u32,                 // 0x10
    pub wait_obj_id: u32,         // 0x14
    _pad1: [u32; 1],              // 0x18
    pub stack_start_addr: u32,    // 0x1c
    pub stack_size: u32,          // 0x20
    pub name: *const u8,          // 0x24
    _pad2: [u32; 2],              // 0x28
    pub self_ptr: *mut Task,      // 0x30
    _pad3: [u32; 3],              // 0x34
    pub task_id: u32,             // 0x40
    _pad4: [u8; 8],               // 0x44 (state / yield / sleep bytes)
    pub context: *mut c_void,     // 0x4c
}

// Layout is only meaningful on the 32-bit camera target.
#[cfg(target_pointer_width = "32")]
const _: () = {
    assert!(core::mem::size_of::<Task>() == 0x50);
    assert!(core::mem::offset_of!(Task, name) == 0x24);
    assert!(core::mem::offset_of!(Task, self_ptr) == 0x30);
    assert!(core::mem::offset_of!(Task, task_id) == 0x40);
    assert!(core::mem::offset_of!(Task, context) == 0x4c);
};

extern "C" {
    // Task Management
    pub fn task_create(
        name: *const u8,
        priority: u32,
        stack_size: u32,
        entry: *const c_void,
        arg: *mut c_void,
    ) -> *mut Task;

    pub fn msleep(milliseconds: u32);

    // Synchronization Semaphores
    pub fn create_named_semaphore(name: *const u8, starts_unlocked: i32) -> *mut Semaphore;
    pub fn take_semaphore(semaphore: *mut Semaphore, timeout_ms: i32) -> i32;
    pub fn take_semaphore_now(semaphore: *mut Semaphore) -> i32;
    pub fn give_semaphore(semaphore: *mut Semaphore) -> i32;

    // Recursive Locks (Mutex)
    pub fn CreateRecursiveLock(name: *const u8) -> *mut c_void;
    pub fn AcquireRecursiveLock(lock: *mut c_void, timeout: i32) -> *mut c_void;
    pub fn ReleaseRecursiveLock(lock: *mut c_void) -> *mut c_void;

    // Memory Allocators
    pub fn _AllocateMemory(size: usize) -> *mut c_void;
    pub fn _FreeMemory(ptr: *mut c_void);
    pub fn _alloc_dma_memory(size: usize) -> *mut c_void;
    pub fn _free_dma_memory(ptr: *mut c_void);

    // Cache Coherency
    pub fn sync_caches();
    pub fn clean_d_cache();
    pub fn flush_caches();

    // Debugging / Logging
    pub fn qprintf(fmt: *const u8, ...);
}

// Hardware Memory-Mapped IO Addresses for 5D Mark II
pub const CARD_LED_ADDRESS: usize = 0xC022_00BC;
pub const RAW_LV_EDMAC_CHANNEL_ADDR: usize = 0xC0F0_4500;
pub const REG_EDMAC_WRITE_LV_ADDR: usize = 0xC0F2_6208;
pub const REG_EDMAC_WRITE_HD_ADDR: usize = 0xC0F0_4008;

pub const YUV422_LV_BUFFER_1: usize = 0x41B0_7800;
pub const YUV422_LV_BUFFER_2: usize = 0x5C00_7800;
pub const YUV422_LV_BUFFER_3: usize = 0x5F60_7800;
