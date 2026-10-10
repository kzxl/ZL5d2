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

static CONFIG_INT("live_flt.mode", live_flt_mode, 0);       /* 0: Off, 1: Live GND, 2: Monochrome, 3: ND Sim */
static CONFIG_INT("live_flt.gnd_ev", live_flt_gnd_ev, 2);   /* 1 to 4 stops attenuation for GND sky */
static CONFIG_INT("live_flt.gnd_pos", live_flt_gnd_pos, 45); /* Horizon level: 10% to 90% */
static CONFIG_INT("live_flt.gnd_soft", live_flt_gnd_soft, 1);/* 0: Hard transition, 1: Soft transition */
static CONFIG_INT("live_flt.nd_ev", live_flt_nd_ev, 3);     /* 1 to 6 stops attenuation for ND simulation */

static const char * mode_choices[] = { "OFF", "Live GND", "Monochrome B&W", "Live ND Sim" };
static const char * gnd_soft_choices[] = { "Hard Edge", "Soft Edge" };

static unsigned int live_flt_display_cbr(unsigned int ctx)
{
    if (!live_flt_mode || !lv || EXT_MONITOR_CONNECTED)
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
}

static struct menu_entry live_flt_menu[] =
{
    {
        .name = "Live Filter",
        .priv = &live_flt_mode,
        .update = live_flt_main_update,
        .max = 3,
        .choices = mode_choices,
        .help = "Real-time computational optical filters on LiveView LCD.",
        .depends_on = DEP_LIVEVIEW,
        .children = (struct menu_entry[])
        {
            {
                .name = "Filter Mode",
                .priv = &live_flt_mode,
                .max = 3,
                .choices = mode_choices,
                .help = "Select active real-time optical filter simulation.",
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
    menu_add("Overlay", live_flt_menu, COUNT(live_flt_menu));
    return 0;
}

static unsigned int live_flt_deinit()
{
    menu_remove("Overlay", live_flt_menu, COUNT(live_flt_menu));
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
MODULE_CONFIGS_END()
