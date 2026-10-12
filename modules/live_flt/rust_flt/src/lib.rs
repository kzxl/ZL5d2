#![cfg_attr(not(test), no_std)]
#[cfg(not(test))]
use core::panic::PanicInfo;

/// Required by `no_std`. Unreachable in the release object: the Makefile build is checked to
/// contain no undefined symbols, and the hot path has no panic sites (no indexing, no `/`,
/// shifts clamped). Unit tests run on the host with `std` instead.
#[cfg(not(test))]
#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

/// Restoring shift-subtract division (no libcall).
///
/// The ARM946E-S has no hardware divider, so `/` lowers to a `BL __aeabi_uidiv` into core.
/// Rust objects are not built with long calls, and a module loaded more than 32 MB away from
/// core would overflow that branch. Division by zero returns 0.
#[inline(never)]
fn div_u32(n: u32, d: u32) -> u32 {
    if d == 0 {
        return 0;
    }
    let mut quotient = 0u32;
    let mut remainder = 0u64; /* 64-bit: (remainder << 1) cannot drop a bit when d > 2^31 */
    let divisor = d as u64;
    let mut bit = 32;
    while bit > 0 {
        bit -= 1;
        remainder = (remainder << 1) | ((n >> bit) & 1) as u64;
        if remainder >= divisor {
            remainder -= divisor;
            quotient |= 1 << bit;
        }
    }
    quotient
}

#[inline(always)]
fn clamp_u8(val: i32) -> u8 {
    if val < 0 {
        0
    } else if val > 255 {
        255
    } else {
        val as u8
    }
}

/// Transform YUV422 pixel pair using high-speed fixed-point arithmetic (Q8)
#[inline(always)]
fn apply_film_stock(
    preset: u32,
    mut y0: u8,
    mut y1: u8,
    mut u: u8,
    mut v: u8,
) -> (u8, u8, u8, u8) {
    match preset {
        // Kodak Portra 400: Soft highlights, lifted shadows, warm skin tones
        1 => {
            y0 = clamp_u8(16 + ((y0 as i32 * 240) >> 8));
            y1 = clamp_u8(16 + ((y1 as i32 * 240) >> 8));

            let u_diff = u as i32 - 128;
            let v_diff = v as i32 - 128;
            // 92% -> (diff * 235) >> 8; 112% -> (diff * 287) >> 8
            u = clamp_u8(128 + ((u_diff * 235) >> 8));
            v = clamp_u8(128 + ((v_diff * 287) >> 8));
        }

        // Fuji Velvia 50: Vivid high-contrast S-curve, punchy greens and blues
        2 => {
            let s_curve = |y: u8| -> u8 {
                let yi = y as i32;
                if yi < 128 {
                    clamp_u8((yi * yi) >> 7)
                } else {
                    let inv = 255 - yi;
                    clamp_u8(255 - ((inv * inv) >> 7))
                }
            };
            y0 = s_curve(y0);
            y1 = s_curve(y1);

            let u_diff = u as i32 - 128;
            let v_diff = v as i32 - 128;
            // 140% -> (diff * 358) >> 8; 135% -> (diff * 346) >> 8
            u = clamp_u8(128 + ((u_diff * 358) >> 8));
            v = clamp_u8(128 + ((v_diff * 346) >> 8));
        }

        // Cinestill 800T: Tungsten cool shadows, warm red halation on highlights
        3 => {
            let u_diff = u as i32 - 128;
            let v_diff = v as i32 - 128;
            // 125% -> (diff * 320) >> 8; 90% -> (diff * 230) >> 8
            u = clamp_u8(128 + ((u_diff * 320) >> 8));
            v = clamp_u8(128 + ((v_diff * 230) >> 8));

            // Red halation glow on highlights
            if y0 > 185 {
                let glow = (y0 as i32 - 185) >> 2;
                v = clamp_u8(v as i32 + glow);
            }
            if y1 > 185 {
                let glow = (y1 as i32 - 185) >> 2;
                v = clamp_u8(v as i32 + glow);
            }
        }

        // Kodak Tri-X 400: Punchy high-contrast monochrome
        4 => {
            let tri_curve = |y: u8| -> u8 {
                let yi = y as i32;
                if yi < 128 {
                    clamp_u8((yi * 220) >> 8)
                } else {
                    clamp_u8(110 + (((yi - 128) * 290) >> 8))
                }
            };
            y0 = tri_curve(y0);
            y1 = tri_curve(y1);
            u = 128;
            v = 128;
        }

        // Classic Chrome: Muted documentary colors, hard shadow gradations
        5 => {
            let chrome_shadow = |y: u8| -> u8 {
                let yi = y as i32;
                if yi < 64 {
                    clamp_u8((yi * 220) >> 8)
                } else {
                    y
                }
            };
            y0 = chrome_shadow(y0);
            y1 = chrome_shadow(y1);

            let u_diff = u as i32 - 128;
            let v_diff = v as i32 - 128;
            // 75% -> (diff * 192) >> 8
            u = clamp_u8(128 + ((u_diff * 192) >> 8));
            v = clamp_u8(128 + ((v_diff * 192) >> 8));
        }

        _ => {}
    }

    (y0, y1, u, v)
}

#[inline(always)]
fn apply_cine_mask(
    style: u32,
    is_border: bool,
    mut y0: u8,
    mut y1: u8,
    mut u: u8,
    mut v: u8,
) -> (u8, u8, u8, u8) {
    match style {
        // Viewfinder Shaded (50% dimming outside, solid white border)
        0 => {
            if is_border {
                y0 = 235;
                y1 = 235;
                u = 128;
                v = 128;
            } else {
                y0 >>= 1;
                y1 >>= 1;
            }
        }

        // Cinema Solid Black (100% black letterbox bars)
        1 => {
            y0 = 16;
            y1 = 16;
            u = 128;
            v = 128;
        }

        // Frame Lines (thin guide lines, background unchanged)
        2 => {
            if is_border {
                y0 = 235;
                y1 = 235;
                u = 128;
                v = 128;
            }
        }

        _ => {}
    }

    (y0, y1, u, v)
}

/// Computes the visible (uncropped) rectangle for a cine aspect-ratio mask.
///
/// # Safety
/// Each output pointer must be either null (skipped) or valid for a write of one `i32`.
#[no_mangle]
pub unsafe extern "C" fn rust_flt_get_crop_bounds(
    ratio: u32,
    width: i32,
    height: i32,
    ymin: *mut i32,
    ymax: *mut i32,
    xmin: *mut i32,
    xmax: *mut i32,
) {
    let (mut ym, mut y_max, mut xm, mut x_max) = (0, height, 0, width);

    match ratio {
        // Hasselblad XPan (65:24 / ~2.70:1) -> 720x266 (107px bars)
        1 => {
            let bar_h = (height * 107) >> 9; // exact 107px for 480p
            ym = bar_h;
            y_max = height - bar_h;
        }

        // CinemaScope (2.39:1) -> 720x301 (89px bars)
        2 => {
            let bar_h = (height * 89) >> 9; // exact 89px for 480p
            ym = bar_h;
            y_max = height - bar_h;
        }

        // Univisium (2.00:1) -> 720x360 (60px bars)
        3 => {
            let bar_h = (height * 60) >> 9; // exact 60px for 480p
            ym = bar_h;
            y_max = height - bar_h;
        }

        // Square (1:1) -> 480x480 (120px bars)
        4 => {
            let bar_w = (width - height) >> 1;
            xm = bar_w;
            x_max = width - bar_w;
        }

        // Classic (4:3) -> 640x480 (40px bars)
        5 => {
            let bar_w = (width * 40) >> 9; // exact 40px for 720p
            xm = bar_w;
            x_max = width - bar_w;
        }

        _ => {}
    }

    // SAFETY: each pointer is checked for null; non-null pointers are valid per the contract above.
    if !ymin.is_null() { *ymin = ym; }
    if !ymax.is_null() { *ymax = y_max; }
    if !xmin.is_null() { *xmin = xm; }
    if !xmax.is_null() { *xmax = x_max; }
}

#[inline(always)]
fn apply_recipe(
    mut y0: u8,
    mut y1: u8,
    mut u: u8,
    mut v: u8,
    highlight: i32,
    shadow: i32,
    saturation: i32,
    wb_ab: i32,
    wb_gm: i32,
) -> (u8, u8, u8, u8) {
    if highlight != 0 {
        let adj_h = |y: u8| -> u8 {
            let yi = y as i32;
            if yi > 128 {
                let dy = yi - 128;
                clamp_u8(128 + ((dy * (256 + highlight * 28)) >> 8))
            } else {
                y
            }
        };
        y0 = adj_h(y0);
        y1 = adj_h(y1);
    }

    if shadow != 0 {
        let adj_s = |y: u8| -> u8 {
            let yi = y as i32;
            if yi < 128 {
                if shadow > 0 {
                    clamp_u8(yi + shadow * 6)
                } else {
                    clamp_u8((yi * (256 + shadow * 36)) >> 8)
                }
            } else {
                y
            }
        };
        y0 = adj_s(y0);
        y1 = adj_s(y1);
    }

    if saturation != 0 {
        let sat_mult = 256 + saturation * 44;
        let u_diff = u as i32 - 128;
        let v_diff = v as i32 - 128;
        u = clamp_u8(128 + ((u_diff * sat_mult) >> 8));
        v = clamp_u8(128 + ((v_diff * sat_mult) >> 8));
    }

    if wb_ab != 0 {
        u = clamp_u8(u as i32 - wb_ab * 2);
        v = clamp_u8(v as i32 + wb_ab * 2);
    }
    if wb_gm != 0 {
        u = clamp_u8(u as i32 - wb_gm * 2);
        v = clamp_u8(v as i32 - wb_gm * 2);
    }

    (y0, y1, u, v)
}

/// Grades one LiveView UYVY row: cine mask, graduated/plain ND preview, film stock and recipe.
///
/// # Safety
/// - `row_src` must be valid for reads of `dwords_visible` `u32` values (one UYVY pixel pair each)
///   and 4-byte aligned.
/// - `row_dst` must be valid for writes of `dwords_visible` `u32` values and 4-byte aligned.
/// - `row_src` and `row_dst` must either be identical (in-place) or not overlap: each element is
///   read once before its own slot is written, never after.
///
/// Null pointers or `dwords_visible <= 0` make the call a no-op. Shift amounts are clamped to 0..7.
#[no_mangle]
pub unsafe extern "C" fn rust_flt_process_row(
    row_src: *const u32,
    row_dst: *mut u32,
    dwords_visible: i32,
    y: i32,
    ymin: i32,
    ymax: i32,
    xmin: i32,
    xmax: i32,
    has_crop: i32,
    mask_style: u32,
    film_preset: u32,
    gnd_active: i32,
    gnd_shift: i32,
    gnd_y_start: i32,
    gnd_y_end: i32,
    nd_active: i32,
    nd_shift: i32,
    recipe_active: i32,
    rec_highlight: i32,
    rec_shadow: i32,
    rec_saturation: i32,
    rec_wb_ab: i32,
    rec_wb_gm: i32,
) {
    if row_src.is_null() || row_dst.is_null() {
        return;
    }

    let row_outside_v = (has_crop != 0) && (y < ymin || y >= ymax);
    let row_border_v  = (has_crop != 0) && (y == ymin || y == ymax - 1);

    // Row-invariant ND parameters, hoisted out of the per-pixel loop.
    let gnd_s = gnd_shift.clamp(0, 7) as u32;
    let nd_s = nd_shift.clamp(0, 7) as u32;
    let gnd_full = gnd_active != 0 && y < gnd_y_start;
    let gnd_ramp = gnd_active != 0 && !gnd_full && y <= gnd_y_end;
    let gnd_factor: i32 = if gnd_ramp {
        // y is in [gnd_y_start, gnd_y_end] here, so both operands are non-negative and denom >= 1.
        let factor_min = 256 >> gnd_s;
        let denom = (gnd_y_end - gnd_y_start + 1).max(1) as u32;
        let numer = ((256 - factor_min) * (y - gnd_y_start)) as u32;
        (factor_min + div_u32(numer, denom) as i32).min(256)
    } else {
        256
    };

    for x in 0..dwords_visible {
        // SAFETY: 0 <= x < dwords_visible and row_src is valid for that many reads (contract).
        let p = *row_src.offset(x as isize);
        let mut u  = (p & 0xFF) as u8;
        let mut y0 = ((p >> 8) & 0xFF) as u8;
        let mut v  = ((p >> 16) & 0xFF) as u8;
        let mut y1 = ((p >> 24) & 0xFF) as u8;

        let px0 = x * 2;
        let px1 = px0 + 1;

        let is_outside = row_outside_v || ((has_crop != 0) && (px1 < xmin || px0 >= xmax));
        let is_border  = row_border_v  || ((has_crop != 0) && (px0 == xmin || px1 == xmax - 1));

        if is_outside {
            let res = apply_cine_mask(mask_style, is_border, y0, y1, u, v);
            y0 = res.0;
            y1 = res.1;
            u  = res.2;
            v  = res.3;
        } else {
            if gnd_active != 0 {
                if gnd_full {
                    y0 >>= gnd_s;
                    y1 >>= gnd_s;
                } else if gnd_ramp {
                    y0 = ((y0 as i32 * gnd_factor) >> 8) as u8;
                    y1 = ((y1 as i32 * gnd_factor) >> 8) as u8;
                }
            } else if nd_active != 0 {
                y0 >>= nd_s;
                y1 >>= nd_s;
            } else {
                if film_preset > 0 && film_preset <= 5 {
                    let res = apply_film_stock(film_preset, y0, y1, u, v);
                    y0 = res.0;
                    y1 = res.1;
                    u  = res.2;
                    v  = res.3;
                }
                if recipe_active != 0 {
                    let rec = apply_recipe(y0, y1, u, v, rec_highlight, rec_shadow, rec_saturation, rec_wb_ab, rec_wb_gm);
                    y0 = rec.0;
                    y1 = rec.1;
                    u  = rec.2;
                    v  = rec.3;
                }
            }
        }

        let out_pixel = (u as u32) | ((y0 as u32) << 8) | ((v as u32) << 16) | ((y1 as u32) << 24);
        // SAFETY: same index bound as the read; row_dst is valid for dwords_visible writes (contract).
        *row_dst.offset(x as isize) = out_pixel;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Original per-pixel GND gradient formula, kept as the reference oracle.
    fn reference_gnd(y0: u8, y: i32, shift: i32, start: i32, end: i32) -> u8 {
        let factor_min = 256 >> shift;
        let denom = (end - start + 1).max(1);
        let factor = factor_min + ((256 - factor_min) * (y - start)) / denom;
        let factor_clamped = if factor > 256 { 256 } else { factor };
        ((y0 as i32 * factor_clamped) >> 8) as u8
    }

    fn run_row(src: &[u32], y: i32, gnd: (i32, i32, i32, i32), nd: (i32, i32)) -> [u32; 8] {
        let mut dst = [0u32; 8];
        unsafe {
            rust_flt_process_row(
                src.as_ptr(), dst.as_mut_ptr(), 8, y, 0, 480, 0, 720, 0, 0, 0,
                gnd.0, gnd.1, gnd.2, gnd.3, nd.0, nd.1, 0, 0, 0, 0, 0, 0,
            );
        }
        dst
    }

    #[test]
    fn div_u32_matches_hardware_division() {
        for n in [0u32, 1, 7, 255, 256, 65_535, 65_280, 1 << 20, u32::MAX] {
            for d in [1u32, 2, 3, 7, 13, 255, 256, 480, 65_535, u32::MAX] {
                assert_eq!(div_u32(n, d), n / d, "{} / {}", n, d);
            }
        }
        assert_eq!(div_u32(123, 0), 0, "division by zero is defined as 0");
    }

    #[test]
    fn gnd_gradient_matches_reference() {
        let src: [u32; 8] = [0x80FF_80FF, 0x8010_8020, 0x80C8_8064, 0, 0xFFFF_FFFF, 0x1234_5678, 0x80EB_80EB, 0x8001_8002];
        for shift in 1..=4 {
            for (start, end) in [(100, 300), (0, 479), (200, 200), (50, 51)] {
                for y in start..=end {
                    let out = run_row(&src, y, (1, shift, start, end), (0, 0));
                    for (i, p) in src.iter().enumerate() {
                        let y0 = ((p >> 8) & 0xFF) as u8;
                        let y1 = ((p >> 24) & 0xFF) as u8;
                        assert_eq!(((out[i] >> 8) & 0xFF) as u8, reference_gnd(y0, y, shift, start, end));
                        assert_eq!(((out[i] >> 24) & 0xFF) as u8, reference_gnd(y1, y, shift, start, end));
                        assert_eq!(out[i] & 0x00FF_00FF, p & 0x00FF_00FF, "chroma untouched");
                    }
                }
            }
        }
    }

    #[test]
    fn out_of_range_shifts_do_not_panic() {
        let src = [0xFFFF_FFFFu32; 8];
        for s in [-5, -1, 0, 7, 8, 9, 31, 1000] {
            let _ = run_row(&src, 10, (1, s, 100, 200), (0, 0));
            let _ = run_row(&src, 150, (1, s, 100, 200), (0, 0));
            let _ = run_row(&src, 10, (0, 0, 0, 0), (1, s));
        }
        let out = run_row(&src, 10, (0, 0, 0, 0), (1, 100));
        assert_eq!((out[0] >> 8) & 0xFF, 0xFF >> 7, "nd shift is clamped to 7");
    }

    #[test]
    fn crop_bounds_accept_null_outputs() {
        unsafe {
            rust_flt_get_crop_bounds(2, 720, 480, core::ptr::null_mut(), core::ptr::null_mut(),
                                     core::ptr::null_mut(), core::ptr::null_mut());
        }
        let (mut a, mut b, mut c, mut d) = (0, 0, 0, 0);
        unsafe { rust_flt_get_crop_bounds(2, 720, 480, &mut a, &mut b, &mut c, &mut d) };
        assert_eq!((a, b, c, d), (83, 397, 0, 720));
    }
}
