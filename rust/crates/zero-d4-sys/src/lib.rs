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

#[repr(C)]
pub struct Task {
    pub prev_task: *mut Task,
    pub next_task: *mut Task,
    pub run_prio: u32,
    pub entry: *mut c_void,
    pub arg: u32,
    pub wait_obj_id: u32,
    _pad1: [u32; 1],
    pub stack_start_addr: u32,
    pub stack_size: u32,
    pub name: *const u8,
    _pad2: [u32; 3],
    pub self_ptr: *mut Task,
    _pad3: [u32; 3],
    pub task_id: u32,
    _pad4: [u8; 7],
    pub context: *mut c_void,
}

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
