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
#include <beep.h>

/* Configuration parameters */
static CONFIG_INT("live_flt.film_sim",   live_flt_film_sim, 0);     /* 0: Off, 1: Portra, 2: Velvia, 3: Cinestill, 4: Tri-X, 5: Classic Chrome, 6: GND, 7: ND */
static CONFIG_INT("live_flt.cine_ratio", live_flt_cine_ratio, 0);   /* 0: Off, 1: XPan, 2: 2.39:1, 3: 2.00:1, 4: 1:1, 5: 4:3 */
static CONFIG_INT("live_flt.cine_style", live_flt_cine_style, 0);   /* 0: Shaded, 1: Solid Black, 2: Lines */
static CONFIG_INT("live_flt.gnd_ev",     live_flt_gnd_ev, 2);       /* 1 to 4 stops attenuation */
static CONFIG_INT("live_flt.gnd_pos",    live_flt_gnd_pos, 45);     /* 10% to 90% */
static CONFIG_INT("live_flt.gnd_soft",   live_flt_gnd_soft, 1);    /* 0: Hard, 1: Soft */
static CONFIG_INT("live_flt.nd_ev",      live_flt_nd_ev, 3);        /* 1 to 6 stops */
static CONFIG_INT("live_flt.osd_badge",  live_flt_osd_badge, 1);    /* Show OSD badge */

/* Custom Film Recipe configuration */
static CONFIG_INT("live_flt.recipe_preset", live_flt_recipe_preset, 0); /* 0: Off, 1: Portra Warm, 2: Fuji Superia, 3: Kodachrome 64, 4: Tri-X Push 2, 5: Custom */
static CONFIG_INT("live_flt.rec_base",       live_flt_rec_base, 0);     /* 0: Portra, 1: Velvia, 2: Cinestill, 3: Tri-X, 4: Classic Chrome */
static CONFIG_INT("live_flt.rec_high",       live_flt_rec_high, 0);     /* -2 .. +2 */
static CONFIG_INT("live_flt.rec_shadow",     live_flt_rec_shadow, 0);   /* -2 .. +2 */
static CONFIG_INT("live_flt.rec_sat",        live_flt_rec_sat, 0);      /* -2 .. +2 */
static CONFIG_INT("live_flt.rec_wb_ab",      live_flt_rec_wb_ab, 0);    /* -9 .. +9 */
static CONFIG_INT("live_flt.rec_wb_gm",      live_flt_rec_wb_gm, 0);    /* -9 .. +9 */
static CONFIG_INT("live_flt.rec_grain",      live_flt_rec_grain, 0);    /* 0..3 */

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

static const char * recipe_preset_choices[] = {
    "OFF",
    "Portra Warm",
    "Fuji Superia",
    "Kodachrome 64",
    "Tri-X Push 2",
    "Custom Recipe"
};

static const char * recipe_base_choices[] = {
    "Kodak Portra 400",
    "Fuji Velvia 50",
    "Cinestill 800T",
    "Kodak Tri-X 400",
    "Classic Chrome"
};

static const char * grain_choices[] = {
    "Off",
    "Subtle",
    "Medium",
    "Heavy"
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
    int nd_shift,
    int recipe_active,
    int rec_highlight,
    int rec_shadow,
    int rec_saturation,
    int rec_wb_ab,
    int rec_wb_gm
);

static void get_effective_recipe_params(
    int * base, int * high, int * shadow, int * sat, int * wb_ab, int * wb_gm, int * grain, const char ** name
)
{
    switch (live_flt_recipe_preset)
    {
        case 1: // Portra Warm
            *base = 1; // Portra 400
            *high = -1;
            *shadow = 1;
            *sat = 0;
            *wb_ab = 3;
            *wb_gm = 1;
            *grain = 1;
            if (name) *name = "Portra Warm";
            break;

        case 2: // Fuji Superia
            *base = 2; // Velvia 50
            *high = 0;
            *shadow = -1;
            *sat = -1;
            *wb_ab = -2;
            *wb_gm = 2;
            *grain = 2;
            if (name) *name = "Fuji Superia";
            break;

        case 3: // Kodachrome 64
            *base = 5; // Classic Chrome
            *high = 1;
            *shadow = 1;
            *sat = 1;
            *wb_ab = 4;
            *wb_gm = -2;
            *grain = 2;
            if (name) *name = "Kodachrome 64";
            break;

        case 4: // Tri-X Push 2
            *base = 4; // Kodak Tri-X 400
            *high = 2;
            *shadow = -2;
            *sat = 0;
            *wb_ab = 0;
            *wb_gm = 0;
            *grain = 3;
            if (name) *name = "Tri-X Push 2";
            break;

        case 5: // Custom
            *base = live_flt_rec_base + 1; // 0..4 mapped to 1..5
            *high = live_flt_rec_high;
            *shadow = live_flt_rec_shadow;
            *sat = live_flt_rec_sat;
            *wb_ab = live_flt_rec_wb_ab;
            *wb_gm = live_flt_rec_wb_gm;
            *grain = live_flt_rec_grain;
            if (name) *name = "Custom Recipe";
            break;

        default:
            *base = 0;
            *high = 0;
            *shadow = 0;
            *sat = 0;
            *wb_ab = 0;
            *wb_gm = 0;
            *grain = 0;
            if (name) *name = "Standard Look";
            break;
    }
}

static void recipe_save_custom(void * priv, int delta)
{
    FIO_CreateDirectory("A:/ML/RECIPES");
    FILE * f = FIO_CreateFile("A:/ML/RECIPES/CUSTOM.REC");
    if (!f) {
        bmp_printf(FONT_MED, 0, 60, "Failed to save recipe");
        return;
    }
    char buf[128];
    snprintf(buf, sizeof(buf),
        "BASE=%d\n"
        "HIGH=%d\n"
        "SHADOW=%d\n"
        "SAT=%d\n"
        "WB_AB=%d\n"
        "WB_GM=%d\n"
        "GRAIN=%d\n",
        live_flt_rec_base,
        live_flt_rec_high,
        live_flt_rec_shadow,
        live_flt_rec_sat,
        live_flt_rec_wb_ab,
        live_flt_rec_wb_gm,
        live_flt_rec_grain
    );
    FIO_WriteFile(f, buf, strlen(buf));
    FIO_CloseFile(f);
    beep();
    bmp_printf(FONT_MED, 0, 60, "Saved to CUSTOM.REC");
}

static void recipe_load_custom(void * priv, int delta)
{
    FILE * f = FIO_OpenFile("A:/ML/RECIPES/CUSTOM.REC", O_RDONLY | O_SYNC);
    if (!f) {
        bmp_printf(FONT_MED, 0, 60, "CUSTOM.REC not found");
        return;
    }
    char buf[256];
    int size = FIO_ReadFile(f, buf, sizeof(buf) - 1);
    FIO_CloseFile(f);
    if (size <= 0) return;
    buf[size] = '\0';

    char * line = buf;
    while (line && *line)
    {
        char * next = strchr(line, '\n');
        if (next) *next = '\0';

        if (strncmp(line, "BASE=", 5) == 0) live_flt_rec_base = atoi(line + 5);
        else if (strncmp(line, "HIGH=", 5) == 0) live_flt_rec_high = atoi(line + 5);
        else if (strncmp(line, "SHADOW=", 7) == 0) live_flt_rec_shadow = atoi(line + 7);
        else if (strncmp(line, "SAT=", 4) == 0) live_flt_rec_sat = atoi(line + 4);
        else if (strncmp(line, "WB_AB=", 6) == 0) live_flt_rec_wb_ab = atoi(line + 6);
        else if (strncmp(line, "WB_GM=", 6) == 0) live_flt_rec_wb_gm = atoi(line + 6);
        else if (strncmp(line, "GRAIN=", 6) == 0) live_flt_rec_grain = atoi(line + 6);

        line = next ? next + 1 : NULL;
    }
    live_flt_rec_base = COERCE(live_flt_rec_base, 0, 4);
    live_flt_rec_high = COERCE(live_flt_rec_high, -2, 2);
    live_flt_rec_shadow = COERCE(live_flt_rec_shadow, -2, 2);
    live_flt_rec_sat = COERCE(live_flt_rec_sat, -2, 2);
    live_flt_rec_wb_ab = COERCE(live_flt_rec_wb_ab, -9, 9);
    live_flt_rec_wb_gm = COERCE(live_flt_rec_wb_gm, -9, 9);
    live_flt_rec_grain = COERCE(live_flt_rec_grain, 0, 3);
    live_flt_recipe_preset = 5; // Custom
    beep();
    bmp_printf(FONT_MED, 0, 60, "Loaded CUSTOM.REC");
}

static int curve_insp_in_progress = 0;

static void recipe_inspect_curve_task(void * priv)
{
    curve_insp_in_progress = 1;
    gui_stop_menu();
    msleep(400);

    int base = 0, high = 0, shadow = 0, sat = 0, wb_ab = 0, wb_gm = 0, grain = 0;
    const char * name = NULL;
    get_effective_recipe_params(&base, &high, &shadow, &sat, &wb_ab, &wb_gm, &grain, &name);

    int win_x = 90, win_y = 50, win_w = 540, win_h = 360;
    bmp_fill(COLOR_BLACK, win_x, win_y, win_w, win_h);
    bmp_draw_rect(COLOR_WHITE, win_x, win_y, win_w, win_h);
    bmp_draw_rect(COLOR_GRAY(50), win_x + 2, win_y + 2, win_w - 4, win_h - 4);

    bmp_printf(FONT_LARGE, win_x + 20, win_y + 15, "TONE CURVE INSPECTOR");

    /* Graph coordinates */
    int gx = win_x + 30, gy = win_y + 70, gw = 220, gh = 220;
    bmp_fill(COLOR_BLACK, gx, gy, gw, gh);
    bmp_draw_rect(COLOR_GRAY(50), gx, gy, gw, gh);

    /* Grid lines at 25%, 50%, 75% */
    for (int p = 1; p <= 3; p++)
    {
        int y = gy + gh - (p * gh / 4);
        int x = gx + (p * gw / 4);
        draw_line(gx, y, gx + gw, y, COLOR_GRAY(20));
        draw_line(x, gy, x, gy + gh, COLOR_GRAY(20));
    }

    /* Neutral 1:1 reference line */
    draw_line(gx, gy + gh, gx + gw, gy, COLOR_GRAY(50));

    /* Plot actual non-linear tone curve */
    int prev_px = gx;
    int prev_py = gy + gh;

    for (int i = 0; i <= gw; i++)
    {
        int x_in = (i * 255) / gw;
        int dy = 0;
        if (x_in > 128)
            dy = (high * (x_in - 128) * (255 - x_in)) / 256;
        else
            dy = (shadow * x_in * (128 - x_in)) / 256;

        int y_out = COERCE(x_in + dy, 0, 255);
        int px = gx + i;
        int py = gy + gh - (y_out * gh / 255);

        if (i > 0)
        {
            draw_line(prev_px, prev_py, px, py, COLOR_YELLOW);
        }
        prev_px = px;
        prev_py = py;
    }

    /* Metadata details panel */
    int tx = gx + gw + 30;
    int ty = gy + 10;
    int dy_line = 24;

    bmp_printf(FONT_MED, tx, ty + 0 * dy_line, "Recipe:    %s", name ? name : "None");
    bmp_printf(FONT_MED, tx, ty + 1 * dy_line, "Base Stock:%s", (base > 0 && base <= 5) ? recipe_base_choices[base - 1] : "Standard");
    bmp_printf(FONT_MED, tx, ty + 2 * dy_line, "Highlight: %+d", high);
    bmp_printf(FONT_MED, tx, ty + 3 * dy_line, "Shadow:    %+d", shadow);
    bmp_printf(FONT_MED, tx, ty + 4 * dy_line, "Saturation:%+d", sat);
    bmp_printf(FONT_MED, tx, ty + 5 * dy_line, "WB A/B:    %+d", wb_ab);
    bmp_printf(FONT_MED, tx, ty + 6 * dy_line, "WB G/M:    %+d", wb_gm);
    bmp_printf(FONT_MED, tx, ty + 7 * dy_line, "Film Grain:%s", grain_choices[COERCE(grain, 0, 3)]);

    bmp_printf(FONT_SMALL, win_x + 30, win_y + win_h - 26, "Half-shutter or wait 5s to close...");

    /* Wait for half-shutter or timeout */
    for (int t = 0; t < 50; t++)
    {
        if (get_halfshutter_pressed()) break;
        msleep(100);
    }
    clrscr();
    redraw();
    curve_insp_in_progress = 0;
}

static void recipe_inspect_curve(void * priv, int delta)
{
    if (curve_insp_in_progress) return;
    task_create("curve_insp", 0x1c, 0x2000, recipe_inspect_curve_task, 0);
}

static unsigned int live_flt_display_cbr(unsigned int ctx)
{
    if ((!live_flt_film_sim && !live_flt_cine_ratio && !live_flt_recipe_preset) || !lv)
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

    int rec_active = (live_flt_recipe_preset > 0);
    int rec_base = 0, rec_h = 0, rec_s = 0, rec_sat = 0, rec_ab = 0, rec_gm = 0, rec_grain = 0;
    if (rec_active)
    {
        get_effective_recipe_params(&rec_base, &rec_h, &rec_s, &rec_sat, &rec_ab, &rec_gm, &rec_grain, NULL);
    }

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
            rec_active ? (uint32_t)rec_base : (uint32_t)live_flt_film_sim,
            gnd_active,
            top_shift,
            y_start_trans,
            y_end_trans,
            nd_active,
            nd_shift,
            rec_active,
            rec_h,
            rec_s,
            rec_sat,
            rec_ab,
            rec_gm
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

static MENU_UPDATE_FUNC(live_flt_recipe_update)
{
    if (!live_flt_recipe_preset)
    {
        MENU_SET_VALUE("OFF");
    }
    else
    {
        MENU_SET_VALUE("%s", recipe_preset_choices[COERCE(live_flt_recipe_preset, 0, 5)]);
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
        .name = "Film Recipes",
        .priv = &live_flt_recipe_preset,
        .update = live_flt_recipe_update,
        .max = 5,
        .choices = recipe_preset_choices,
        .help = "Fujifilm-style custom film recipes (tone curves, WB shift, grain).",
        .depends_on = DEP_LIVEVIEW,
        .children = (struct menu_entry[])
        {
            {
                .name = "Preset",
                .priv = &live_flt_recipe_preset,
                .max = 5,
                .choices = recipe_preset_choices,
                .help = "Select film recipe preset or Custom.",
            },
            {
                .name = "Base Stock",
                .priv = &live_flt_rec_base,
                .max = 4,
                .choices = recipe_base_choices,
                .help = "Base emulation stock for Custom recipe.",
            },
            {
                .name = "Highlight Tone",
                .priv = &live_flt_rec_high,
                .min = -2,
                .max = 2,
                .help = "Highlight rolloff (-2 soft to +2 hard).",
            },
            {
                .name = "Shadow Tone",
                .priv = &live_flt_rec_shadow,
                .min = -2,
                .max = 2,
                .help = "Shadow level (-2 crushed to +2 lifted).",
            },
            {
                .name = "Color / Sat",
                .priv = &live_flt_rec_sat,
                .min = -2,
                .max = 2,
                .help = "Color saturation (-2 muted to +2 punchy).",
            },
            {
                .name = "WB Shift A/B",
                .priv = &live_flt_rec_wb_ab,
                .min = -9,
                .max = 9,
                .help = "White balance Amber (+)/Blue (-) bias.",
            },
            {
                .name = "WB Shift G/M",
                .priv = &live_flt_rec_wb_gm,
                .min = -9,
                .max = 9,
                .help = "White balance Green (+)/Magenta (-) bias.",
            },
            {
                .name = "Film Grain",
                .priv = &live_flt_rec_grain,
                .max = 3,
                .choices = grain_choices,
                .help = "LFSR Analog grain level.",
            },
            {
                .name = "Save to CUSTOM.REC",
                .select = recipe_save_custom,
                .help = "Export recipe parameters to A:/ML/RECIPES/CUSTOM.REC.",
            },
            {
                .name = "Load from CUSTOM.REC",
                .select = recipe_load_custom,
                .help = "Import recipe parameters from A:/ML/RECIPES/CUSTOM.REC.",
            },
            {
                .name = "Tone Curve Inspector",
                .select = recipe_inspect_curve,
                .help = "Display live graphical tone curve & recipe summary on LCD.",
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
    MODULE_CONFIG(live_flt_recipe_preset)
    MODULE_CONFIG(live_flt_rec_base)
    MODULE_CONFIG(live_flt_rec_high)
    MODULE_CONFIG(live_flt_rec_shadow)
    MODULE_CONFIG(live_flt_rec_sat)
    MODULE_CONFIG(live_flt_rec_wb_ab)
    MODULE_CONFIG(live_flt_rec_wb_gm)
    MODULE_CONFIG(live_flt_rec_grain)
MODULE_CONFIGS_END()
