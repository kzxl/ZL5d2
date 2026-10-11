#![no_std]
use core::panic::PanicInfo;

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

#[no_mangle]
pub extern "C" fn __aeabi_unwind_cpp_pr0() {}

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
    let row_outside_v = (has_crop != 0) && (y < ymin || y >= ymax);
    let row_border_v  = (has_crop != 0) && (y == ymin || y == ymax - 1);

    for x in 0..dwords_visible {
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
                if y < gnd_y_start {
                    y0 >>= gnd_shift;
                    y1 >>= gnd_shift;
                } else if y <= gnd_y_end {
                    let factor_min = 256 >> gnd_shift;
                    let denom = (gnd_y_end - gnd_y_start + 1).max(1);
                    let factor = factor_min + ((256 - factor_min) * (y - gnd_y_start)) / denom;
                    let factor_clamped = if factor > 256 { 256 } else { factor };
                    y0 = ((y0 as i32 * factor_clamped) >> 8) as u8;
                    y1 = ((y1 as i32 * factor_clamped) >> 8) as u8;
                }
            } else if nd_active != 0 {
                y0 >>= nd_shift;
                y1 >>= nd_shift;
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
        *row_dst.offset(x as isize) = out_pixel;
    }
}
