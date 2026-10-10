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

/* Rust Engine FFI Declarations */
extern void rust_flt_get_crop_bounds(
    uint32_t ratio,
    int width,
    int height,
    int * ymin,
    int * ymax,
    int * xmin,
    int * xmax
);

extern void rust_flt_process_row(
    const uint32_t * row_src,
    uint32_t * row_dst,
    int dwords_visible,
    int y,
    int ymin,
    int ymax,
    int xmin,
    int xmax,
    int has_crop,
    uint32_t mask_style,
    uint32_t film_preset,
    int gnd_active,
    int gnd_shift,
    int gnd_y_start,
    int gnd_y_end,
    int nd_active,
    int nd_shift
);

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
    rust_flt_get_crop_bounds(live_flt_cine_ratio, width, height, &ymin, &ymax, &xmin, &xmax);

    int has_crop = (live_flt_cine_ratio > 0);
    uint32_t mask_style = (uint32_t) live_flt_cine_style;

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

        rust_flt_process_row(
            row_src,
            row_dst,
            dwords_visible,
            y,
            ymin,
            ymax,
            xmin,
            xmax,
            has_crop,
            mask_style,
            (uint32_t) live_flt_film_sim,
            gnd_active,
            top_shift,
            y_start_trans,
            y_end_trans,
            nd_active,
            nd_shift
        );

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
