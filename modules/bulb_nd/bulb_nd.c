/**
 * BULB utilities for ND filters
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
 */

#include <module.h>
#include <dryos.h>
#include <property.h>
#include <menu.h>
#include <config.h>
#include <lens.h>
#include <shoot.h>
#include <bmp.h>
#include <beep.h>
#include <histogram.h>
#include <raw.h>
#include <tasks.h>

static CONFIG_INT("bulb_nd.enabled", bulb_nd_enabled, 0);
static CONFIG_INT("bulb_nd.ev.x2", bulb_nd_ev_x2, 0);

int display_idle();
static int set_pressed = 0;
static int last_valid_shutter = 0;
static int running_measure = 0;
static int measured_pic_1 = 0;

#define BULB_ND_MEASURE_STATE_WAIT_PIC_1   1
#define BULB_ND_MEASURE_STATE_PIC_1        2
#define BULB_ND_MEASURE_STATE_WAIT_PIC_2   3
#define BULB_ND_MEASURE_STATE_PIC_2        4

static const char* get_nd_filter_label(int ev_x2)
{
    switch(ev_x2) {
        case 2:  return "ND2 (1 stop)";
        case 4:  return "ND4 (2 stops)";
        case 6:  return "ND8 (3 stops)";
        case 8:  return "ND16 (4 stops)";
        case 10: return "ND32 (5 stops)";
        case 12: return "ND64 (6 stops)";
        case 14: return "ND128 (7 stops)";
        case 16: return "ND256 (8 stops)";
        case 18: return "ND512 (9 stops)";
        case 20: return "ND1000 (10 stops)";
        case 24: return "ND4000 (12 stops)";
        case 30: return "ND32000 (15 stops)";
        case 33: return "Solar (16.5 stops)";
        default: return "";
    }
}

static const char* nd_preset_choices[] = {
    "Custom / Manual",
    "ND8 (-3 EV)",
    "ND16 (-4 EV)",
    "ND64 (-6 EV)",
    "ND500 (-9 EV)",
    "ND1000 (-10 EV)",
    "ND4000 (-12 EV)",
    "Solar (-16.5 EV)"
};
static const int nd_preset_ev_x2[] = {
    -1, 6, 8, 12, 18, 20, 24, 33
};
static int nd_preset_idx = 0;

static MENU_UPDATE_FUNC(nd_preset_display)
{
    int matched = 0;
    for (int i = 1; i < (int)COUNT(nd_preset_ev_x2); i++) {
        if (bulb_nd_ev_x2 == nd_preset_ev_x2[i]) {
            nd_preset_idx = i;
            matched = 1;
            break;
        }
    }
    if (!matched) nd_preset_idx = 0;
    MENU_SET_VALUE("%s", nd_preset_choices[nd_preset_idx]);
    if(is_bulb_mode())
    {
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Does not work in BULB (will auto switch to BULB)");
    }
    if(!bulb_nd_enabled)
    {
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Bulb ND is disabled");
    }
}

static MENU_SELECT_FUNC(nd_preset_select)
{
    nd_preset_idx = (nd_preset_idx + delta + COUNT(nd_preset_choices)) % COUNT(nd_preset_choices);
    if (nd_preset_idx > 0) {
        bulb_nd_ev_x2 = nd_preset_ev_x2[nd_preset_idx];
    }
}

static MENU_UPDATE_FUNC(buld_nd_display)
{
    if(is_bulb_mode())
    {
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Does not work in BULB (will auto switch to BULB)");
    }
    else
    {
        MENU_SET_WARNING(MENU_WARN_INFO, "BULB duration = current shutter + %d.%d EV", bulb_nd_ev_x2 / 2, (bulb_nd_ev_x2 % 2) * 5);
    }
    if(bulb_nd_enabled)
    {
        const char *nd_lbl = get_nd_filter_label(bulb_nd_ev_x2);
        if (nd_lbl[0])
            MENU_SET_VALUE("+%d.%d EV (%s)", bulb_nd_ev_x2 / 2, (bulb_nd_ev_x2 % 2) * 5, nd_lbl);
        else
            MENU_SET_VALUE("+%d.%d EV", bulb_nd_ev_x2 / 2, (bulb_nd_ev_x2 % 2) * 5);
    }
}

static MENU_UPDATE_FUNC(buld_nd_amount_display)
{
    const char *nd_lbl = get_nd_filter_label(bulb_nd_ev_x2);
    if (nd_lbl[0])
        MENU_SET_VALUE("%d.%d EV - %s", bulb_nd_ev_x2 / 2, (bulb_nd_ev_x2 % 2) * 5, nd_lbl);
    else
        MENU_SET_VALUE("%d.%d EV", bulb_nd_ev_x2 / 2, (bulb_nd_ev_x2 % 2) * 5);
    if(is_bulb_mode())
    {
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Does not work in BULB (will auto switch to BULB)");
    }
    if(!bulb_nd_enabled)
    {
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Bulb ND is disabled");
    }
}

static MENU_UPDATE_FUNC(bulb_nd_measure_display)
{
    if(is_bulb_mode())
    {
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Does not work in BULB (will auto switch to BULB)");
    }
    if(!bulb_nd_enabled)
    {
        MENU_SET_WARNING(MENU_WARN_NOT_WORKING, "Bulb ND is disabled");
    }
}

static void bulb_nd_measure_task(int unused)
{
    if(running_measure == BULB_ND_MEASURE_STATE_PIC_1)
    {
        if (!raw_update_params())
        {
            NotifyBox(5000, "Raw error");
            running_measure = 0;
            return;
        }
        measured_pic_1 = raw_hist_get_percentile_level(500, GRAY_PROJECTION_MEDIAN_RGB, 0);
        //get out of QR
        SW1(1,50);
        SW1(0,0);
        running_measure++;
    }
    else if(running_measure == BULB_ND_MEASURE_STATE_PIC_2)
    {
        if (!raw_update_params())
        {
            NotifyBox(5000, "Raw error");
            running_measure = 0;
            return;
        }
        int measured_pic_2 = raw_hist_get_percentile_level(500, GRAY_PROJECTION_MEDIAN_RGB, 0);
        if (measured_pic_1 <= 0 || measured_pic_2 <= 0)
        {
            NotifyBox(5000, "ND Measure failed: invalid RAW levels");
            running_measure = 0;
            return;
        }
        float ev = (bulb_nd_ev_x2 / 2.0F) + raw_to_ev(measured_pic_1) - raw_to_ev(measured_pic_2);
        if (ev != ev || ev < 0.0F || ev > 30.0F)
        {
            NotifyBox(5000, "ND Measure: calculation error");
            running_measure = 0;
            return;
        }
        int ev_x10 = (int)roundf(ev * 10);
        NotifyBox(10000, "Measured ND Strength: %d.%d EV", ev_x10 / 10, ev_x10 % 10);
        bulb_nd_ev_x2 = COERCE((int)roundf(ev * 2), 0, 40);
        running_measure = 0;
    }
    else
    {
        running_measure = 0;
    }
}

static MENU_SELECT_FUNC(bulb_nd_measure)
{
    if(bulb_nd_enabled && !running_measure)
    {
        gui_stop_menu();
        running_measure = 1;
    }
}

PROP_HANDLER(PROP_GUI_STATE)
{
    int* data = (int*)buf;
    if (data[0] == GUISTATE_QR)
    {
        if(running_measure == BULB_ND_MEASURE_STATE_WAIT_PIC_1 || running_measure == BULB_ND_MEASURE_STATE_WAIT_PIC_2)
        {
            running_measure ++;
            task_create("bulb_nd_measure_task", 0x1c, 0x1000, bulb_nd_measure_task, (void*) 0);
        }
    }
}

static unsigned int bulb_nd_shoot_cbr()
{
    if(!bulb_nd_enabled)
    {
        set_pressed = 0;
        running_measure = 0;
        return 0;
    }
    if(is_movie_mode() || is_bulb_mode() || !display_idle() || gui_menu_shown())
    {
        set_pressed = 0;
        return 0;
    }
    if(lens_info.raw_shutter) last_valid_shutter = lens_info.raw_shutter;
    int64_t raw_ms = raw2shutter_ms(last_valid_shutter);
    int64_t duration_calc = ((int64_t)powi(2, bulb_nd_ev_x2 / 2) * (bulb_nd_ev_x2 % 2 ? 3 : 2) * raw_ms) / 2;
    /* Prevent integer overflow with extreme ND filters; cap at 8 hours (28800s) */
    #define BULB_ND_MAX_DURATION_MS 28800000
    if (duration_calc > BULB_ND_MAX_DURATION_MS)
    {
        duration_calc = BULB_ND_MAX_DURATION_MS;
    }
    int bulb_duration = (int)duration_calc;
    
    bmp_printf(FONT_LARGE, 0, 0, "BULB (+%2d.%d EV):%5d\"   ", bulb_nd_ev_x2 / 2, (bulb_nd_ev_x2 % 2) * 5, bulb_duration / 1000);
    
    if(running_measure == BULB_ND_MEASURE_STATE_WAIT_PIC_1)
    {
        bmp_printf(FONT_LARGE, 50, 50, "Take a picture without the ND filter\nUse a tripod and a static scene");
    }
    else if(running_measure == BULB_ND_MEASURE_STATE_WAIT_PIC_2)
    {
        bmp_printf(FONT_LARGE, 50, 50, "Attach ND filter and hold SET/Joystick");
    }
    
    if (set_pressed && job_state_ready_to_take_pic() && get_ms_clock() - set_pressed > 1000)
    {
        beep();
        info_led_blink(1,50,50); // short blink so you know bulb timer was triggered
        info_led_on();
        set_pressed = 0;
        if(bulb_duration < 1000)
        {
            NotifyBox(10000, "Expo too short for BULB!");
            beep();
            beep();
            return 0;
        }
        
        NotifyBox(10000, "[HalfShutter] Bulb timer: %d\"", bulb_duration / 1000);
        int m0 = shooting_mode;
        wait_till_next_second();
        NotifyBox(2000, "[2s] Bulb timer: %d\"", bulb_duration / 1000);
        info_led_on();
        wait_till_next_second();
        if (get_halfshutter_pressed() || !display_idle() || m0 != shooting_mode || !job_state_ready_to_take_pic())
        {
            NotifyBox(2000, "Bulb timer canceled.");
            info_led_off();
            return 0;
        }
        NotifyBox(2000, "[1s] Bulb timer: %d\"", bulb_duration / 1000);
        info_led_on();
        wait_till_next_second();
        if (get_halfshutter_pressed() || !display_idle() || m0 != shooting_mode || !job_state_ready_to_take_pic())
        {
            NotifyBox(2000, "Bulb timer canceled.");
            info_led_off();
            return 0;
        }
        info_led_off();
        bulb_take_pic(bulb_duration);
    }
    
    return 0;
}


static unsigned int bulb_nd_keypress_cbr(unsigned int key)
{
    if(is_movie_mode() || is_bulb_mode() || gui_menu_shown() || !display_idle() || !job_state_ready_to_take_pic()) return 1;
    
    if((key == MODULE_KEY_PRESS_SET) ||
       (key == MODULE_KEY_JOY_CENTER) )
    {
        set_pressed = get_ms_clock();
        return 0;
    }
    else if((key == MODULE_KEY_UNPRESS_SET) ||
            (key == MODULE_KEY_UNPRESS_UDLR) )
    {
        set_pressed = 0;
        return 0;
    }
    return 1;
}

static struct menu_entry bulb_nd_menu[] =
{
    {
        .name = "Bulb ND",
        .priv = &bulb_nd_enabled,
        .update = buld_nd_display,
        .max = 1,
        .help  = "Hold SET to take a bulb exposure based on current expo settings",
        .depends_on = DEP_PHOTO_MODE,
        .children = (struct menu_entry[])
        {
            {
                .name = "ND Preset",
                .select = nd_preset_select,
                .update = nd_preset_display,
                .help = "Quick select standard optical ND filter (ND8, ND64, ND1000...)",
            },
            {
                .name = "ND Strength",
                .priv = &bulb_nd_ev_x2,
                .update = buld_nd_amount_display,
                .max = 40,
                .help  = "Strength of ND Filter in EV",
                .help2  = "Used to compute bulb time from current settings",
            },
            {
                .name = "Measure ND",
                .select = bulb_nd_measure,
                .update = bulb_nd_measure_display,
                .help = "Measure the strength of an ND filter",
                .help2 = "Enter/guess approximate strength above before beginning",
            },
            MENU_EOL
        },
    },
};

static unsigned int bulb_nd_init()
{
    menu_add("Shoot", bulb_nd_menu, COUNT(bulb_nd_menu));

    return 0;
}

static unsigned int bulb_nd_deinit()
{
    menu_remove("Shoot", bulb_nd_menu, COUNT(bulb_nd_menu));
    set_pressed = 0;
    running_measure = 0;
    return 0;
}

MODULE_INFO_START()
    MODULE_INIT(bulb_nd_init)
    MODULE_DEINIT(bulb_nd_deinit)
MODULE_INFO_END()

MODULE_CBRS_START()
    MODULE_CBR(CBR_SHOOT_TASK, bulb_nd_shoot_cbr, 0)
    MODULE_CBR(CBR_KEYPRESS, bulb_nd_keypress_cbr, 0)
MODULE_CBRS_END()

MODULE_PROPHANDLERS_START()
    MODULE_PROPHANDLER(PROP_GUI_STATE)
MODULE_PROPHANDLERS_END()

MODULE_CONFIGS_START()
    MODULE_CONFIG(bulb_nd_enabled)
    MODULE_CONFIG(bulb_nd_ev_x2)
MODULE_CONFIGS_END()
