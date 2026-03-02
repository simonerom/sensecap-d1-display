// =============================================================================
// ui.h — Hardware display/touch init declarations
//
// The full UI logic lives in ScreenManager.
// This file only exposes the three hardware init functions that
// must run before LVGL is used, plus the global touch indev pointer.
// =============================================================================
#pragma once

#include <lvgl.h>

// Global touch indev pointer, set by lvgl_touch_init().
// Used by ScreenManager for gesture detection via lv_indev_get_vect().
extern lv_indev_t* g_touch_indev;

// Initialize ST7701S display (Arduino_GFX RGB panel + LVGL display driver).
void lvgl_display_init();

// Initialize FT5x06 touch controller + LVGL input device.
void lvgl_touch_init();

// Start the esp_timer that calls lv_tick_inc(5) every 5 ms.
void lvgl_tick_timer_init();
