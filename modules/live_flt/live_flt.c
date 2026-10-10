/**
 * Live Filter & Cinematic Framing Module (live_flt) for Magic Lantern / ZeroLantern
 * Real-time computational film simulations and aspect ratio framing for Canon 5D Mark II (DIGIC 4)
 *
 * Supported features:
 * 1. Film Simulations:
 *    - Kodak Portra 400 (Warm skin tones, pastel palette, soft highlight rolloff)
 *    - Fuji Velvia 50 (Punchy contrast, vivid landscape saturation)
 *    - Cinestill 800T (Cool tungsten shadows, warm highlight halation)
 *    - Kodak Tri-X 400 (High-contrast dramatic street monochrome)
 *    - Classic Chrome (Muted tones, rich shadow contrast)
 *    - Electronic Live GND (Graduated Neutral Density for skies/horizons)
 *    - Live ND Simulator (1 to 6 stops attenuation)
 *
 * 2. Cinematic Aspect Ratio Framing:
 *    - Hasselblad XPan (65:24 / 2.70:1 Panoramic)
 *    - CinemaScope (2.39:1 Anamorphic)
 *    - Univisium (2.00:1 / 18:9 Modern Streaming)
 *    - Square Medium Format (1:1 / 6x6)
 *    - Classic 645 Format (4:3 Academy)
 *
 * 3. Mask Styles:
 *    - Viewfinder Shaded (50% dimmed outside area, rangefinder style)
 *    - Cinema Solid Black (Pure black letterbox/pillarbox)
 *    - Frame Lines (Subtle boundary borders)
 *
 * Copyright (C) 2026 Magic Lantern Project & ZeroLantern Contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#include <module.h>
#include <dryos.h>
#include <property.h>
#include <menu.h>
#include <config.h>
#include <bmp.h>
#include <vram.h>
#include <string.h>

/* Configuration parameters */
static CONFIG_INT("live_flt.film_sim",   live_flt_film_sim, 0);     /* 0: Off, 1: Portra, 2: Velvia, 3: Cinestill, 4: Tri-X, 5: Classic Chrome, 6: GND, 7: ND */
static CONFIG_INT("live_flt.cine_ratio", live_flt_cine_ratio, 0);   /* 0: Off, 1: XPan, 2: 2.39:1, 3: 2.00:1, 4: 1:1, 5: 4:3 */
static CONFIG_INT("live_flt.cine_style", live_flt_cine_style, 0);   /* 0: Shaded, 1: Solid Black, 2: Lines */
static CONFIG_INT("live_flt.gnd_ev",     live_flt_gnd_ev, 2);       /* 1 to 4 stops attenuation */
static CONFIG_INT("live_flt.gnd_pos",    live_flt_gnd_pos, 45);     /* 10% to 90% */
static CONFIG_INT("live_flt.gnd_soft",   live_flt_gnd_soft, 1);    /* 0: Hard, 1: Soft */
static CONFIG_INT("live_flt.nd_ev",      live_flt_nd_ev, 3);        /* 1 to 6 stops */
static CONFIG_INT("live_flt.osd_badge",  live_flt_osd_badge, 1);    /* Show OSD badge */

static const char * film_sim_choices[] = {
    "OFF",
    "Kodak Portra 400",
    "Fuji Velvia 50",
    "Cinestill 800T",
    "Kodak Tri-X 400",
    "Classic Chrome",
    "Live GND",
    "Live ND Sim"
};

static const char * cine_ratio_choices[] = {
    "OFF (3:2)",
    "XPan 65:24 (2.70:1)",
    "Cinema 2.39:1",
    "Univisium 2.00:1",
    "Square 1:1",
    "Classic 4:3"
};

static const char * cine_style_choices[] = {
    "Viewfinder Shaded",
    "Cinema Solid Black",
    "Frame Lines"
};

static const char * gnd_soft_choices[] = {
    "Hard Edge",
    "Soft Edge"
};

static inline uint8_t clamp_u8(int v)
{
    if (v < 0) return 0;
    if (v > 255) return 255;
    return (uint8_t)v;
}

/* Transform a macro-pixel (Y0, Y1, U, V) with active film simulation curve */
static inline void transform_film(int mode, uint8_t *y0, uint8_t *y1, uint8_t *u, uint8_t *v)
{
    switch (mode)
    {
        case 1: /* Kodak Portra 400 */
        {
            if (*y0 > 165) *y0 = 165 + ((*y0 - 165) * 65) / 100;
            else if (*y0 < 35) *y0 = 8 + (*y0 * 75) / 100;

            if (*y1 > 165) *y1 = 165 + ((*y1 - 165) * 65) / 100;
            else if (*y1 < 35) *y1 = 8 + (*y1 * 75) / 100;

            int du = (int)*u - 128;
            int dv = (int)*v - 128;
            *u = clamp_u8(128 + (du * 92) / 100);
            *v = clamp_u8(128 + (dv * 115) / 100 + 4);
            break;
        }

        case 2: /* Fuji Velvia 50 */
        {
            *y0 = (*y0 < 128) ? (*y0 * *y0) / 140 : 255 - ((255 - *y0) * (255 - *y0)) / 140;
            *y1 = (*y1 < 128) ? (*y1 * *y1) / 140 : 255 - ((255 - *y1) * (255 - *y1)) / 140;

            int du = (int)*u - 128;
            int dv = (int)*v - 128;
            *u = clamp_u8(128 + (du * 140) / 100);
            *v = clamp_u8(128 + (dv * 140) / 100);
            break;
        }

        case 3: /* Cinestill 800T */
        {
            int du = ((int)*u - 128) * 110 / 100 + 8;
            int dv = ((int)*v - 128) * 95 / 100 - 3;

            int y_avg = ((int)*y0 + (int)*y1) >> 1;
            if (y_avg > 180)
            {
                dv += ((y_avg - 180) * 45) / 75;
            }

            *u = clamp_u8(128 + du);
            *v = clamp_u8(128 + dv);
            break;
        }

        case 4: /* Kodak Tri-X 400 */
        {
            *u = 128;
            *v = 128;
            *y0 = (*y0 < 128) ? (*y0 * *y0) / 135 : 255 - ((255 - *y0) * (255 - *y0)) / 135;
            *y1 = (*y1 < 128) ? (*y1 * *y1) / 135 : 255 - ((255 - *y1) * (255 - *y1)) / 135;
            break;
        }

        case 5: /* Classic Chrome */
        {
            if (*y0 < 110) *y0 = (*y0 * *y0) / 120;
            if (*y1 < 110) *y1 = (*y1 * *y1) / 120;

            int du = (int)*u - 128;
            int dv = (int)*v - 128;
            *u = clamp_u8(128 + (du * 75) / 100);
            *v = clamp_u8(128 + (dv * 80) / 100);
            break;
        }

        default:
            break;
    }
}

/* Apply masking outside the active cinematic crop area */
static inline void apply_mask(int style, int is_border, uint8_t *y0, uint8_t *y1, uint8_t *u, uint8_t *v)
{
    if (style == 2) /* Frame Lines */
    {
        if (is_border)
        {
            *y0 = 230;
            *y1 = 230;
            *u = 128;
            *v = 128;
        }
    }
    else if (style == 1) /* Solid Cinema Black */
    {
        *y0 = 0;
        *y1 = 0;
        *u = 128;
        *v = 128;
    }
    else /* Shaded Viewfinder (50% dimmed) */
    {
        *y0 = *y0 >> 1;
        *y1 = *y1 >> 1;
        *u = 128 + (((int)*u - 128) >> 1);
        *v = 128 + (((int)*v - 128) >> 1);
    }
}

/* Compute vertical and horizontal crop boundaries for chosen aspect ratio */
static void get_crop_bounds(int ratio, int width, int height, int *ymin, int *ymax, int *xmin, int *xmax)
{
    *ymin = 0;
    *ymax = height;
    *xmin = 0;
    *xmax = width;

    switch (ratio)
    {
        case 1: /* Hasselblad XPan 65:24 (~2.708:1) */
        {
            int h = (width * 24) / 65;
            if (h > height) h = height;
            *ymin = (height - h) / 2;
            *ymax = *ymin + h;
            break;
        }

        case 2: /* CinemaScope 2.39:1 */
        {
            int h = (width * 100) / 239;
            if (h > height) h = height;
            *ymin = (height - h) / 2;
            *ymax = *ymin + h;
            break;
        }

        case 3: /* Univisium 2.00:1 */
        {
            int h = width / 2;
            if (h > height) h = height;
            *ymin = (height - h) / 2;
            *ymax = *ymin + h;
            break;
        }

        case 4: /* Square 1:1 */
        {
            int w = height;
            if (w > width) w = width;
            *xmin = (width - w) / 2;
            *xmax = *xmin + w;
            break;
        }

        case 5: /* Classic 4:3 */
        {
            int w = (height * 4) / 3;
            if (w > width) w = width;
            *xmin = (width - w) / 2;
            *xmax = *xmin + w;
            break;
        }

        default:
            break;
    }
}

static unsigned int live_flt_display_cbr(unsigned int ctx)
{
    if ((!live_flt_film_sim && !live_flt_cine_ratio) || !lv)
    {
        return 0;
    }

    if (ctx == 0)
    {
        return 1;
    }

    struct display_filter_buffers * buffers = (struct display_filter_buffers *) ctx;
    if (!buffers || !buffers->src_buf || !buffers->dst_buf)
    {
        return 0;
    }

    uint32_t * src = (uint32_t *) buffers->src_buf;
    uint32_t * dst = (uint32_t *) buffers->dst_buf;

    struct vram_info * vram = get_yuv422_vram();
    int width = vram ? vram->width : 720;
    int height = vram ? vram->height : 480;

    if (width <= 0 || height <= 0 || width > 1056 || height > 704)
    {
        width = 720;
        height = 480;
    }

    int pitch_bytes = (vram && vram->pitch) ? vram->pitch : (width * 2);
    if (pitch_bytes < width * 2 || pitch_bytes > 4096)
    {
        pitch_bytes = width * 2;
    }
    int dwords_per_row = pitch_bytes / 4;
    int dwords_visible = (width * 2) / 4;
    if (dwords_per_row <= 0 || dwords_visible <= 0)
    {
        return 0;
    }
    if (dwords_visible > dwords_per_row)
    {
        dwords_visible = dwords_per_row;
    }

    int ymin, ymax, xmin, xmax;
    get_crop_bounds(live_flt_cine_ratio, width, height, &ymin, &ymax, &xmin, &xmax);

    int has_crop = (live_flt_cine_ratio > 0);
    int mask_style = live_flt_cine_style;

    int gnd_active = (live_flt_film_sim == 6);
    int horizon_y = 0, y_start_trans = 0, y_end_trans = 0, top_shift = 0;
    if (gnd_active)
    {
        horizon_y = (height * live_flt_gnd_pos) / 100;
        int trans_range = live_flt_gnd_soft ? (height * 14) / 100 : 4;
        if (trans_range < 2) trans_range = 2;
        y_start_trans = horizon_y - trans_range;
        if (y_start_trans < 0) y_start_trans = 0;
        y_end_trans = horizon_y + trans_range;
        if (y_end_trans > height) y_end_trans = height;
        top_shift = COERCE(live_flt_gnd_ev, 1, 4);
    }

    int nd_active = (live_flt_film_sim == 7);
    int nd_shift = COERCE(live_flt_nd_ev, 1, 6);

    for (int y = 0; y < height; y++)
    {
        uint32_t * row_src = src + y * dwords_per_row;
        uint32_t * row_dst = dst + y * dwords_per_row;

        int row_outside_v = has_crop && (y < ymin || y >= ymax);
        int row_border_v  = has_crop && (y == ymin || y == ymax - 1);

        for (int x = 0; x < dwords_visible; x++)
        {
            uint32_t p = row_src[x];
            uint8_t u  = p & 0xFF;
            uint8_t y0 = (p >> 8) & 0xFF;
            uint8_t v  = (p >> 16) & 0xFF;
            uint8_t y1 = (p >> 24) & 0xFF;

            int px0 = x * 2;
            int px1 = px0 + 1;

            int is_outside = row_outside_v || (has_crop && (px1 < xmin || px0 >= xmax));
            int is_border  = row_border_v  || (has_crop && (px0 == xmin || px1 == xmax - 1));

            if (is_outside)
            {
                apply_mask(mask_style, is_border, &y0, &y1, &u, &v);
            }
            else
            {
                if (gnd_active)
                {
                    if (y < y_start_trans)
                    {
                        y0 >>= top_shift;
                        y1 >>= top_shift;
                    }
                    else if (y <= y_end_trans)
                    {
                        int factor_min = 256 >> top_shift;
                        int factor = factor_min + ((256 - factor_min) * (y - y_start_trans)) / (y_end_trans - y_start_trans + 1);
                        if (factor > 256) factor = 256;
                        y0 = (y0 * factor) >> 8;
                        y1 = (y1 * factor) >> 8;
                    }
                }
                else if (nd_active)
                {
                    y0 >>= nd_shift;
                    y1 >>= nd_shift;
                }
                else if (live_flt_film_sim > 0)
                {
                    transform_film(live_flt_film_sim, &y0, &y1, &u, &v);
                }

                if (mask_style == 2 && is_border)
                {
                    y0 = 230; y1 = 230; u = 128; v = 128;
                }
            }

            row_dst[x] = (uint32_t)u | ((uint32_t)y0 << 8) | ((uint32_t)v << 16) | ((uint32_t)y1 << 24);
        }

        if (dwords_per_row > dwords_visible && row_dst != row_src)
        {
            memcpy(row_dst + dwords_visible, row_src + dwords_visible, (dwords_per_row - dwords_visible) * sizeof(uint32_t));
        }
    }

    return 1;
}

static MENU_UPDATE_FUNC(live_flt_film_update)
{
    if (!live_flt_film_sim)
    {
        MENU_SET_VALUE("OFF");
    }
    else if (live_flt_film_sim == 6)
    {
        MENU_SET_VALUE("Live GND (-%d EV, %d%%)", live_flt_gnd_ev, live_flt_gnd_pos);
    }
    else if (live_flt_film_sim == 7)
    {
        MENU_SET_VALUE("Live ND (-%d EV)", live_flt_nd_ev);
    }
    else
    {
        MENU_SET_VALUE("%s", film_sim_choices[live_flt_film_sim]);
    }
}

static MENU_UPDATE_FUNC(live_flt_cine_update)
{
    if (!live_flt_cine_ratio)
    {
        MENU_SET_VALUE("OFF (3:2)");
    }
    else
    {
        MENU_SET_VALUE("%s", cine_ratio_choices[live_flt_cine_ratio]);
    }
}

static struct menu_entry live_flt_menu[] =
{
    {
        .name = "Film Simulation",
        .priv = &live_flt_film_sim,
        .update = live_flt_film_update,
        .max = 7,
        .choices = film_sim_choices,
        .help = "Real-time film color simulations for LiveView & snapshots.",
        .depends_on = DEP_LIVEVIEW,
        .children = (struct menu_entry[])
        {
            {
                .name = "Preset",
                .priv = &live_flt_film_sim,
                .max = 7,
                .choices = film_sim_choices,
                .help = "Select film stock simulation look.",
            },
            {
                .name = "GND Sky Atten",
                .priv = &live_flt_gnd_ev,
                .min = 1,
                .max = 4,
                .help = "Graduated ND sky attenuation strength (stops).",
            },
            {
                .name = "GND Horizon Pos",
                .priv = &live_flt_gnd_pos,
                .min = 10,
                .max = 90,
                .help = "Horizon line level from top (% of height).",
            },
            {
                .name = "GND Transition",
                .priv = &live_flt_gnd_soft,
                .max = 1,
                .choices = gnd_soft_choices,
                .help = "Soft or hard falloff gradient around the horizon.",
            },
            {
                .name = "ND Sim Reduction",
                .priv = &live_flt_nd_ev,
                .min = 1,
                .max = 6,
                .help = "Simulated Neutral Density reduction (stops).",
            },
            MENU_EOL
        },
    },
    {
        .name = "Cinematic Frame",
        .priv = &live_flt_cine_ratio,
        .update = live_flt_cine_update,
        .max = 5,
        .choices = cine_ratio_choices,
        .help = "Aspect ratio framing masks (XPan 65:24, 2.39:1 Cinema, 1:1 Square).",
        .depends_on = DEP_LIVEVIEW,
        .children = (struct menu_entry[])
        {
            {
                .name = "Aspect Ratio",
                .priv = &live_flt_cine_ratio,
                .max = 5,
                .choices = cine_ratio_choices,
                .help = "Select cinematic aspect ratio guide.",
            },
            {
                .name = "Mask Style",
                .priv = &live_flt_cine_style,
                .max = 2,
                .choices = cine_style_choices,
                .help = "Shaded viewfinder dimming, solid black cinema, or border lines.",
            },
            {
                .name = "OSD Ratio Label",
                .priv = &live_flt_osd_badge,
                .max = 1,
                .help = "Display active ratio tag on LiveView screen.",
            },
            MENU_EOL
        },
    },
};

static unsigned int live_flt_init()
{
    menu_add("Look", live_flt_menu, COUNT(live_flt_menu));
    return 0;
}

static unsigned int live_flt_deinit()
{
    menu_remove("Look", live_flt_menu, COUNT(live_flt_menu));
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(live_flt_init)
    MODULE_DEINIT(live_flt_deinit)
MODULE_INFO_END()

MODULE_CBRS_START()
    MODULE_CBR(CBR_DISPLAY_FILTER, live_flt_display_cbr, 0)
MODULE_CBRS_END()

MODULE_CONFIGS_START()
    MODULE_CONFIG(live_flt_film_sim)
    MODULE_CONFIG(live_flt_cine_ratio)
    MODULE_CONFIG(live_flt_cine_style)
    MODULE_CONFIG(live_flt_gnd_ev)
    MODULE_CONFIG(live_flt_gnd_pos)
    MODULE_CONFIG(live_flt_gnd_soft)
    MODULE_CONFIG(live_flt_nd_ev)
    MODULE_CONFIG(live_flt_osd_badge)
MODULE_CONFIGS_END()
