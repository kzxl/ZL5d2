/**
 * Live Filter Module (live_flt) for Magic Lantern
 * Real-time computational optical filters for Canon 5D Mark II (DIGIC 4)
 *
 * Supported modes:
 * 1. Electronic Live GND (Graduated Neutral Density)
 * 2. Monochrome B&W Film Preview
 * 3. Live ND Exposure Simulator
 *
 * Copyright (C) 2026 Magic Lantern Project & Contributors
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <module.h>
#include <dryos.h>
#include <property.h>
#include <menu.h>
#include <config.h>
#include <bmp.h>
#include <vram.h>

static CONFIG_INT("live_flt.mode", live_flt_mode, 0);       /* 0: Off, 1: Live GND, 2: Monochrome, 3: ND Sim, 4: Film Sim */
static CONFIG_INT("live_flt.gnd_ev", live_flt_gnd_ev, 2);   /* 1 to 4 stops attenuation for GND sky */
static CONFIG_INT("live_flt.gnd_pos", live_flt_gnd_pos, 45); /* Horizon level: 10% to 90% */
static CONFIG_INT("live_flt.gnd_soft", live_flt_gnd_soft, 1);/* 0: Hard transition, 1: Soft transition */
static CONFIG_INT("live_flt.nd_ev", live_flt_nd_ev, 3);     /* 1 to 6 stops attenuation for ND simulation */
static CONFIG_INT("live_flt.film", live_flt_film, 0);       /* 0: Portra 400, 1: Velvia 50, 2: Kodachrome 64, 3: Classic Chrome, 4: CineStill 800T, 5: Ilford HP5, 6: Tri-X 400, 7: Sepia */

static const char * mode_choices[] = { "OFF", "Live GND", "Monochrome B&W", "Live ND Sim", "Film Simulation" };
static const char * gnd_soft_choices[] = { "Hard Edge", "Soft Edge" };
static const char * film_choices[] = {
    "Kodak Portra 400",
    "Fuji Velvia 50",
    "Kodak Kodachrome 64",
    "Fuji Classic Chrome",
    "CineStill 800T",
    "Ilford HP5+ 400",
    "Kodak Tri-X 400",
    "Sepia Vintage"
};

static unsigned int live_flt_display_cbr(unsigned int ctx)
{
    if (!live_flt_mode || !lv || ext_monitor_hdmi)
    {
        return 0;
    }

    if (ctx == 0)
    {
        /* Tell core this display filter is active */
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

    if (live_flt_mode == 1) /* Electronic Live GND */
    {
        int horizon_y = (height * live_flt_gnd_pos) / 100;
        int trans_range = live_flt_gnd_soft ? (height * 14) / 100 : 4;
        if (trans_range < 2) trans_range = 2;

        int top_atten_shift = live_flt_gnd_ev;
        if (top_atten_shift < 1) top_atten_shift = 1;
        if (top_atten_shift > 4) top_atten_shift = 4;

        int y_start_trans = horizon_y - trans_range;
        if (y_start_trans < 0) y_start_trans = 0;
        int y_end_trans = horizon_y + trans_range;
        if (y_end_trans > height) y_end_trans = height;

        for (int y = 0; y < height; y++)
        {
            uint32_t * row_src = src + y * dwords_per_row;
            uint32_t * row_dst = dst + y * dwords_per_row;

            if (y < y_start_trans)
            {
                /* Sky region: full attenuation */
                for (int x = 0; x < dwords_visible; x++)
                {
                    uint32_t p = row_src[x];
                    uint32_t u  = p & 0xFF;
                    uint32_t y0 = ((p >> 8) & 0xFF) >> top_atten_shift;
                    uint32_t v  = (p >> 16) & 0xFF;
                    uint32_t y1 = ((p >> 24) & 0xFF) >> top_atten_shift;

                    row_dst[x] = u | (y0 << 8) | (v << 16) | (y1 << 24);
                }
            }
            else if (y <= y_end_trans)
            {
                /* Gradient transition zone */
                int factor_min = 256 >> top_atten_shift;
                int factor = factor_min + ((256 - factor_min) * (y - y_start_trans)) / (y_end_trans - y_start_trans + 1);
                if (factor > 256) factor = 256;

                for (int x = 0; x < dwords_visible; x++)
                {
                    uint32_t p = row_src[x];
                    uint32_t u  = p & 0xFF;
                    uint32_t y0 = (((p >> 8) & 0xFF) * factor) >> 8;
                    uint32_t v  = (p >> 16) & 0xFF;
                    uint32_t y1 = (((p >> 24) & 0xFF) * factor) >> 8;

                    row_dst[x] = u | (y0 << 8) | (v << 16) | (y1 << 24);
                }
            }
            else
            {
                /* Ground region: direct copy visible area */
                memcpy(row_dst, row_src, dwords_visible * sizeof(uint32_t));
            }

            /* Preserve scanline padding bytes without corruption */
            if (dwords_per_row > dwords_visible && row_dst != row_src)
            {
                memcpy(row_dst + dwords_visible, row_src + dwords_visible, (dwords_per_row - dwords_visible) * sizeof(uint32_t));
            }
        }
    }
    else if (live_flt_mode == 2) /* Monochrome B&W Film Preview */
    {
        for (int y = 0; y < height; y++)
        {
            uint32_t * row_src = src + y * dwords_per_row;
            uint32_t * row_dst = dst + y * dwords_per_row;

            for (int x = 0; x < dwords_visible; x++)
            {
                uint32_t p = row_src[x];
                uint32_t y0 = (p >> 8) & 0xFF;
                uint32_t y1 = (p >> 24) & 0xFF;

                /* Neutralize chroma to 128 (pure grayscale U=128, V=128) */
                row_dst[x] = 128 | (y0 << 8) | (128 << 16) | (y1 << 24);
            }

            /* Preserve scanline padding bytes */
            if (dwords_per_row > dwords_visible && row_dst != row_src)
            {
                memcpy(row_dst + dwords_visible, row_src + dwords_visible, (dwords_per_row - dwords_visible) * sizeof(uint32_t));
            }
        }
    }
    else if (live_flt_mode == 3) /* Live ND Exposure Simulator */
    {
        int shift = live_flt_nd_ev;
        if (shift < 1) shift = 1;
        if (shift > 6) shift = 6;

        for (int y = 0; y < height; y++)
        {
            uint32_t * row_src = src + y * dwords_per_row;
            uint32_t * row_dst = dst + y * dwords_per_row;

            for (int x = 0; x < dwords_visible; x++)
            {
                uint32_t p = row_src[x];
                uint32_t u  = p & 0xFF;
                uint32_t y0 = ((p >> 8) & 0xFF) >> shift;
                uint32_t v  = (p >> 16) & 0xFF;
                uint32_t y1 = ((p >> 24) & 0xFF) >> shift;

                row_dst[x] = u | (y0 << 8) | (v << 16) | (y1 << 24);
            }

            /* Preserve scanline padding bytes */
            if (dwords_per_row > dwords_visible && row_dst != row_src)
            {
                memcpy(row_dst + dwords_visible, row_src + dwords_visible, (dwords_per_row - dwords_visible) * sizeof(uint32_t));
            }
        }
    }
    else if (live_flt_mode == 4) /* Real-Time Film Simulation */
    {
        int sat = 256, cross = 0, u_tint = 0, v_tint = 0, c_slope = 256, b_lvl = 0;
        switch (live_flt_film)
        {
            case 0: /* Kodak Portra 400 */
                sat = 230; cross = -15; u_tint = -8; v_tint = 12; c_slope = 230; b_lvl = 6;
                break;
            case 1: /* Fuji Velvia 50 */
                sat = 340; cross = 20; u_tint = 10; v_tint = 14; c_slope = 300; b_lvl = 0;
                break;
            case 2: /* Kodak Kodachrome 64 */
                sat = 280; cross = 10; u_tint = -4; v_tint = 22; c_slope = 280; b_lvl = 2;
                break;
            case 3: /* Fuji Classic Chrome */
                sat = 195; cross = 0; u_tint = -6; v_tint = 4; c_slope = 270; b_lvl = 1;
                break;
            case 4: /* CineStill 800T */
                sat = 260; cross = -30; u_tint = 24; v_tint = -12; c_slope = 260; b_lvl = 8;
                break;
            case 5: /* Ilford HP5+ */
                sat = 0; cross = 0; u_tint = 0; v_tint = 0; c_slope = 250; b_lvl = 4;
                break;
            case 6: /* Kodak Tri-X 400 */
                sat = 0; cross = 0; u_tint = 0; v_tint = 0; c_slope = 340; b_lvl = 0;
                break;
            case 7: /* Sepia Vintage */
                sat = 40; cross = 0; u_tint = -20; v_tint = 28; c_slope = 240; b_lvl = 12;
                break;
            default:
                break;
        }

        for (int y = 0; y < height; y++)
        {
            uint32_t * row_src = src + y * dwords_per_row;
            uint32_t * row_dst = dst + y * dwords_per_row;

            for (int x = 0; x < dwords_visible; x++)
            {
                uint32_t p = row_src[x];
                int u_raw  = (int)(p & 0xFF) - 128;
                int y0_raw = (int)((p >> 8) & 0xFF) - 128;
                int v_raw  = (int)((p >> 16) & 0xFF) - 128;
                int y1_raw = (int)((p >> 24) & 0xFF) - 128;

                int y0 = 128 + ((y0_raw * c_slope) >> 8) + b_lvl;
                int y1 = 128 + ((y1_raw * c_slope) >> 8) + b_lvl;
                if (y0 < 0) y0 = 0; else if (y0 > 255) y0 = 255;
                if (y1 < 0) y1 = 0; else if (y1 > 255) y1 = 255;

                int u = 128, v = 128;
                if (sat > 0)
                {
                    int u_scaled = ((u_raw * sat + v_raw * cross) >> 8) + u_tint;
                    int v_scaled = ((v_raw * sat - u_raw * cross) >> 8) + v_tint;
                    u = u_scaled + 128;
                    v = v_scaled + 128;
                    if (u < 0) u = 0; else if (u > 255) u = 255;
                    if (v < 0) v = 0; else if (v > 255) v = 255;
                }

                row_dst[x] = (uint32_t)u | ((uint32_t)y0 << 8) | ((uint32_t)v << 16) | ((uint32_t)y1 << 24);
            }

            if (dwords_per_row > dwords_visible && row_dst != row_src)
            {
                memcpy(row_dst + dwords_visible, row_src + dwords_visible, (dwords_per_row - dwords_visible) * sizeof(uint32_t));
            }
        }
    }

    return 1;
}

static MENU_UPDATE_FUNC(live_flt_main_update)
{
    if (!live_flt_mode)
    {
        MENU_SET_VALUE("OFF");
    }
    else if (live_flt_mode == 1)
    {
        MENU_SET_VALUE("Live GND (-%d EV, %d%%)", live_flt_gnd_ev, live_flt_gnd_pos);
    }
    else if (live_flt_mode == 2)
    {
        MENU_SET_VALUE("Monochrome B&W");
    }
    else if (live_flt_mode == 3)
    {
        MENU_SET_VALUE("Live ND (-%d EV)", live_flt_nd_ev);
    }
    else if (live_flt_mode == 4)
    {
        MENU_SET_VALUE("%s", film_choices[live_flt_film % COUNT(film_choices)]);
    }
}

static struct menu_entry live_flt_menu[] =
{
    {
        .name = "Live Filter",
        .priv = &live_flt_mode,
        .update = live_flt_main_update,
        .max = 4,
        .choices = mode_choices,
        .help = "Real-time optical filters and film simulations on LiveView LCD.",
        .depends_on = DEP_LIVEVIEW,
        .children = (struct menu_entry[])
        {
            {
                .name = "Filter Mode",
                .priv = &live_flt_mode,
                .max = 4,
                .choices = mode_choices,
                .help = "Select active real-time optical or film simulation.",
            },
            {
                .name = "Film Stock",
                .priv = &live_flt_film,
                .max = 7,
                .choices = film_choices,
                .help = "Select legendary film simulation profile.",
            },
            {
                .name = "GND Sky Attenuation",
                .priv = &live_flt_gnd_ev,
                .min = 1,
                .max = 4,
                .help = "Strength of Graduated ND attenuation on the sky (stops).",
            },
            {
                .name = "GND Horizon Level",
                .priv = &live_flt_gnd_pos,
                .min = 10,
                .max = 90,
                .help = "Position of horizon line from top of screen (% height).",
            },
            {
                .name = "GND Transition Type",
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
                .help = "Simulated Neutral Density reduction strength in stops.",
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
    MODULE_CONFIG(live_flt_mode)
    MODULE_CONFIG(live_flt_gnd_ev)
    MODULE_CONFIG(live_flt_gnd_pos)
    MODULE_CONFIG(live_flt_gnd_soft)
    MODULE_CONFIG(live_flt_nd_ev)
    MODULE_CONFIG(live_flt_film)
MODULE_CONFIGS_END()
