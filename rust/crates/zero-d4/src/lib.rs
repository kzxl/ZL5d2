//! Safe Hardware Abstraction Layer for DIGIC 4 / 5D Mark II.
//!
//! Provides RAII synchronization primitives, critical sections,
//! VRAM surfaces, and atomic support on ARMv5TE without an OS kernel.

#![no_std]

use core::cell::UnsafeCell;
use core::ops::{Deref, DerefMut};
use zero_d4_sys as sys;

// ------------------------------------------------------------------------
// Critical Section & Atomics Implementation for ARMv5TE (ARM946E-S)
// ------------------------------------------------------------------------
struct D4CriticalSection;
critical_section::set_impl!(D4CriticalSection);

unsafe impl critical_section::Impl for D4CriticalSection {
    unsafe fn acquire() -> critical_section::RawRestoreState {
        let old_cpsr: u32;
        core::arch::asm!(
            "mrs {0}, CPSR",
            "orr r1, {0}, #0xC0", // Disable IRQ & FIQ
            "msr CPSR_c, r1",
            out(reg) old_cpsr,
            out("r1") _,
            options(nomem, nostack)
        );
        old_cpsr
    }

    unsafe fn release(restore_state: critical_section::RawRestoreState) {
        core::arch::asm!(
            "mrs r1, CPSR",
            "bic r1, r1, #0xC0",
            "and {0}, {0}, #0xC0",
            "orr r1, r1, {0}",
            "msr CPSR_c, r1",
            in(reg) restore_state,
            out("r1") _,
            options(nomem, nostack)
        );
    }
}

// ------------------------------------------------------------------------
// Safe RAII Semaphore
// ------------------------------------------------------------------------
pub struct D4Semaphore {
    raw: *mut sys::Semaphore,
}

unsafe impl Send for D4Semaphore {}
unsafe impl Sync for D4Semaphore {}

impl D4Semaphore {
    pub fn new(name: &'static [u8], starts_unlocked: bool) -> Option<Self> {
        let raw = unsafe {
            sys::create_named_semaphore(name.as_ptr(), if starts_unlocked { 1 } else { 0 })
        };
        if raw.is_null() {
            None
        } else {
            Some(Self { raw })
        }
    }

    #[inline]
    pub fn acquire(&self, timeout_ms: i32) -> bool {
        let res = unsafe { sys::take_semaphore(self.raw, timeout_ms) };
        res == 0
    }

    #[inline]
    pub fn release(&self) {
        unsafe { sys::give_semaphore(self.raw) };
    }
}

// ------------------------------------------------------------------------
// Safe RAII Mutex
// ------------------------------------------------------------------------
pub struct D4Mutex<T> {
    sem: D4Semaphore,
    data: UnsafeCell<T>,
}

unsafe impl<T: Send> Send for D4Mutex<T> {}
unsafe impl<T: Send> Sync for D4Mutex<T> {}

pub struct D4MutexGuard<'a, T> {
    mutex: &'a D4Mutex<T>,
}

impl<T> D4Mutex<T> {
    pub fn new(name: &'static [u8], data: T) -> Option<Self> {
        let sem = D4Semaphore::new(name, true)?;
        Some(Self {
            sem,
            data: UnsafeCell::new(data),
        })
    }

    pub fn lock(&self) -> D4MutexGuard<'_, T> {
        self.sem.acquire(0); // 0 = wait forever in DryOS
        D4MutexGuard { mutex: self }
    }
}

impl<'a, T> Deref for D4MutexGuard<'a, T> {
    type Target = T;
    fn deref(&self) -> &Self::Target {
        unsafe { &*self.mutex.data.get() }
    }
}

impl<'a, T> DerefMut for D4MutexGuard<'a, T> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        unsafe { &mut *self.mutex.data.get() }
    }
}

impl<'a, T> Drop for D4MutexGuard<'a, T> {
    fn drop(&mut self) {
        self.mutex.sem.release();
    }
}

// ------------------------------------------------------------------------
// Safe VRAM Framebuffer Surface (720x480)
// ------------------------------------------------------------------------
pub const VRAM_WIDTH: usize = 720;
pub const VRAM_HEIGHT: usize = 480;

pub struct VramSurface<'a> {
    buffer: &'a mut [u8],
    width: usize,
    height: usize,
}

impl<'a> VramSurface<'a> {
    /// Safely wraps a raw hardware VRAM address into a bounds-checked slice.
    ///
    /// # Safety
    /// Caller must ensure `base_addr` points to valid hardware display RAM.
    pub unsafe fn from_raw(base_addr: usize, width: usize, height: usize, bpp: usize) -> Self {
        let total_bytes = width * height * bpp;
        let slice = core::slice::from_raw_parts_mut(base_addr as *mut u8, total_bytes);
        Self {
            buffer: slice,
            width,
            height,
        }
    }

    #[inline]
    pub fn width(&self) -> usize {
        self.width
    }

    #[inline]
    pub fn height(&self) -> usize {
        self.height
    }

    #[inline]
    pub fn put_pixel_fast(&mut self, x: usize, y: usize, color: u8) {
        if x < self.width && y < self.height {
            self.buffer[y * self.width + x] = color;
        }
    }

    #[inline]
    pub fn clear(&mut self, color: u8) {
        self.buffer.fill(color);
    }
}

// ------------------------------------------------------------------------
// Hardware LED Controller
// ------------------------------------------------------------------------
pub struct CardLed;

impl CardLed {
    pub fn set(on: bool) {
        let addr = sys::CARD_LED_ADDRESS as *mut u32;
        let val: u32 = if on { 0x46 } else { 0x44 };
        unsafe {
            core::ptr::write_volatile(addr, val);
        }
    }
}
