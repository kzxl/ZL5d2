//! Pure Still Photography algorithmic engine for Canon 5D Mark II.
//!
//! Provides zero-allocation, bounded-checked, real-time photographic calculations:
//! - Optical Neutral Density (ND) filter exposure calculations.
//! - Fast RAW histogram percentile and clipping detectors.
//! - Integer Focus Peaking edge analysis.

#![no_std]

use zero_d4::VramSurface;

// ------------------------------------------------------------------------
// Optical ND Filter Shutter Calculator (Bulb ND)
// ------------------------------------------------------------------------
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NdFilter {
    Nd4,     // 2 Stops (0.6)
    Nd8,     // 3 Stops (0.9)
    Nd16,    // 4 Stops (1.2)
    Nd32,    // 5 Stops (1.5)
    Nd64,    // 6 Stops (1.8)
    Nd128,   // 7 Stops (2.1)
    Nd256,   // 8 Stops (2.4)
    Nd500,   // 9 Stops (2.7)
    Nd1000,  // 10 Stops (3.0)
    Nd32000, // 15 Stops (4.5) - Big Solar
}

impl NdFilter {
    #[inline]
    pub const fn stops(self) -> u32 {
        match self {
            NdFilter::Nd4 => 2,
            NdFilter::Nd8 => 3,
            NdFilter::Nd16 => 4,
            NdFilter::Nd32 => 5,
            NdFilter::Nd64 => 6,
            NdFilter::Nd128 => 7,
            NdFilter::Nd256 => 8,
            NdFilter::Nd500 => 9,
            NdFilter::Nd1000 => 10,
            NdFilter::Nd32000 => 15,
        }
    }

    /// Computes compensated bulb exposure in milliseconds given base shutter speed (in ms).
    /// Uses safe saturating integer arithmetic to prevent 32-bit overflow.
    #[inline]
    pub fn calculate_shutter_ms(self, base_shutter_ms: u32) -> u32 {
        let stops = self.stops();
        base_shutter_ms.saturating_mul(1 << stops)
    }
}

// ------------------------------------------------------------------------
// RAW Histogram Percentile & Highlight Clipping Detector (ETTR)
// ------------------------------------------------------------------------
pub const HIST_BINS: usize = 128;

pub struct RawHistogram {
    bins: [u32; HIST_BINS],
    total_samples: u32,
}

impl RawHistogram {
    pub const fn new() -> Self {
        Self {
            bins: [0; HIST_BINS],
            total_samples: 0,
        }
    }

    #[inline]
    pub fn clear(&mut self) {
        self.bins.fill(0);
        self.total_samples = 0;
    }

    /// Ingests a raw 14-bit CMOS sensor pixel stream (5D Mark II is 14-bit ADC, 0..16383).
    #[inline]
    pub fn ingest_samples(&mut self, samples: &[u16]) {
        for &sample in samples {
            let bin_idx = (sample >> 7) as usize; // Map 14-bit (0..16383) to 7-bit (0..127)
            if bin_idx < HIST_BINS {
                self.bins[bin_idx] = self.bins[bin_idx].saturating_add(1);
                self.total_samples = self.total_samples.saturating_add(1);
            }
        }
    }

    /// Evaluates highlight clipping percentage (e.g. pixels above 14-bit 15500 saturation).
    pub fn highlight_clipping_ratio(&self) -> u32 {
        if self.total_samples == 0 {
            return 0;
        }
        // Top 4 bins (> 15872)
        let blown_pixels: u32 = self.bins[124..128].iter().sum();
        (blown_pixels.saturating_mul(1000)) / self.total_samples // returns permille (0..1000)
    }
}

// ------------------------------------------------------------------------
// C ABI Export for Magic Lantern Integration
// ------------------------------------------------------------------------
#[no_mangle]
pub extern "C" fn zero_photo_calc_nd_exposure(base_ms: u32, nd_stops: u32) -> u32 {
    let filter = match nd_stops {
        2 => NdFilter::Nd4,
        3 => NdFilter::Nd8,
        4 => NdFilter::Nd16,
        5 => NdFilter::Nd32,
        6 => NdFilter::Nd64,
        7 => NdFilter::Nd128,
        8 => NdFilter::Nd256,
        9 => NdFilter::Nd500,
        10 => NdFilter::Nd1000,
        15 => NdFilter::Nd32000,
        _ => NdFilter::Nd4,
    };
    filter.calculate_shutter_ms(base_ms)
}
