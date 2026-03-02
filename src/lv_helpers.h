// =============================================================================
// lv_helpers.h — LVGL 8 abstraction layer
//
// All direct lv_* calls in the UI code go through these helpers.
// On a future LVGL 9 migration, only this file needs updating:
//   - lv_disp_drv_t → lv_display_create()
//   - lv_indev_drv_t → lv_indev_create()
//   - lv_scr_load_anim() anim enum renames
//   - Minor style API changes
// =============================================================================
#pragma once
#include <lvgl.h>
#include <Arduino.h>

// =============================================================================
// Color helpers
// =============================================================================

// Parse a 0xRRGGBB integer into an lv_color_t.
inline lv_color_t lv_hlp_hex(uint32_t hex) {
    return lv_color_make((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
}

// Parse a "#RRGGBB" or "RRGGBB" CSS string into lv_color_t.
// Returns white on malformed input.
inline lv_color_t lv_hlp_hex_str(const char* s) {
    if (!s) return lv_color_white();
    if (*s == '#') s++;
    unsigned long v = strtoul(s, nullptr, 16);
    return lv_hlp_hex((uint32_t)v);
}

// =============================================================================
// Font lookup by pixel size
// Uses custom Montserrat fonts with Latin Extended (U+00A0–U+02FF) charset,
// generated with lv_font_conv and stored in src/fonts/.
// =============================================================================

// Forward-declare custom Latin Extended font symbols (defined in src/fonts/*.c)
LV_FONT_DECLARE(lv_font_montserrat_12_latin)
LV_FONT_DECLARE(lv_font_montserrat_14_latin)
LV_FONT_DECLARE(lv_font_montserrat_18_latin)
LV_FONT_DECLARE(lv_font_montserrat_22_latin)
LV_FONT_DECLARE(lv_font_montserrat_24_latin)
LV_FONT_DECLARE(lv_font_montserrat_28_latin)
LV_FONT_DECLARE(lv_font_montserrat_32_latin)
LV_FONT_DECLARE(lv_font_montserrat_48_latin)

LV_FONT_DECLARE(lv_font_montserrat_64_latin);
LV_FONT_DECLARE(lv_font_montserrat_96_latin);
LV_FONT_DECLARE(lv_font_montserrat_18_bold);
LV_FONT_DECLARE(lv_font_montserrat_24_bold);
LV_FONT_DECLARE(lv_font_montserrat_28_bold);
LV_FONT_DECLARE(lv_font_montserrat_32_bold);
LV_FONT_DECLARE(lv_font_montserrat_96_bold);
LV_FONT_DECLARE(lv_font_montserrat_192_bold);

inline const lv_font_t* lv_hlp_font(int size) {
    if (size <= 12) return &lv_font_montserrat_12_latin;
    if (size <= 14) return &lv_font_montserrat_14_latin;
    if (size <= 18) return &lv_font_montserrat_18_latin;
    if (size <= 22) return &lv_font_montserrat_22_latin;
    if (size <= 24) return &lv_font_montserrat_24_latin;
    if (size <= 28) return &lv_font_montserrat_28_latin;
    if (size <= 32) return &lv_font_montserrat_32_latin;
    if (size <= 48) return &lv_font_montserrat_48_latin;
    if (size <= 64) return &lv_font_montserrat_64_latin;
    return &lv_font_montserrat_96_latin;
}

inline const lv_font_t* lv_hlp_font_bold(int size) {
    // Bold available at: 18, 24, 28, 32, 96 — others fall back to nearest
    if (size <= 18) return &lv_font_montserrat_18_bold;
    if (size <= 24) return &lv_font_montserrat_24_bold;
    if (size <= 28) return &lv_font_montserrat_28_bold;
    if (size <= 32) return &lv_font_montserrat_32_bold;
    if (size <= 96) return &lv_font_montserrat_96_bold;
    return &lv_font_montserrat_192_bold;
}

inline const lv_font_t* lv_hlp_font_ex(int size, bool bold) {
    return bold ? lv_hlp_font_bold(size) : lv_hlp_font(size);
}

// =============================================================================
// Object style helpers
// All use selector=0 (main part, normal state) which covers 99% of UI needs.
// LVGL 9 note: selector usage is identical; only enum values may change.
// =============================================================================

inline void lv_hlp_set_bg(lv_obj_t* obj, lv_color_t color, lv_opa_t opa = LV_OPA_COVER) {
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, opa, 0);
}

inline void lv_hlp_set_radius(lv_obj_t* obj, lv_coord_t r) {
    lv_obj_set_style_radius(obj, r, 0);
}

inline void lv_hlp_set_pad_all(lv_obj_t* obj, lv_coord_t p) {
    lv_obj_set_style_pad_all(obj, p, 0);
}

inline void lv_hlp_set_pad_ver(lv_obj_t* obj, lv_coord_t top, lv_coord_t bottom) {
    lv_obj_set_style_pad_top(obj, top, 0);
    lv_obj_set_style_pad_bottom(obj, bottom, 0);
}

inline void lv_hlp_set_border_none(lv_obj_t* obj) {
    lv_obj_set_style_border_width(obj, 0, 0);
}

inline void lv_hlp_no_scroll(lv_obj_t* obj) {
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
}

inline void lv_hlp_set_text_color(lv_obj_t* obj, lv_color_t c) {
    lv_obj_set_style_text_color(obj, c, 0);
}

inline void lv_hlp_set_font(lv_obj_t* obj, const lv_font_t* f) {
    lv_obj_set_style_text_font(obj, f, 0);
}

// Set up a clean container: transparent bg, no border, no scroll.
inline void lv_hlp_clean_cont(lv_obj_t* obj) {
    lv_hlp_set_bg(obj, lv_color_black(), LV_OPA_TRANSP);
    lv_hlp_set_border_none(obj);
    lv_hlp_no_scroll(obj);
    lv_hlp_set_pad_all(obj, 0);
    lv_obj_set_style_pad_column(obj, 0, 0);
    lv_obj_set_style_pad_row(obj, 0, 0);
}

// =============================================================================
// Flex layout helpers
// =============================================================================

inline void lv_hlp_flex_col(lv_obj_t* obj, lv_coord_t gap = 0) {
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(obj, gap, 0);
}

inline void lv_hlp_flex_row(lv_obj_t* obj, lv_coord_t gap = 0) {
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(obj, gap, 0);
}

inline void lv_hlp_flex_grow(lv_obj_t* obj, uint8_t grow = 1) {
    lv_obj_set_flex_grow(obj, grow);
}

// =============================================================================
// Widget factory helpers
// =============================================================================

// Create a plain container child with no decoration.
inline lv_obj_t* lv_hlp_obj(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_hlp_clean_cont(o);
    return o;
}

// Create a styled card container.
inline lv_obj_t* lv_hlp_card(lv_obj_t* parent, lv_color_t bg, lv_coord_t radius, lv_coord_t pad) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_hlp_set_bg(o, bg);
    lv_hlp_set_radius(o, radius);
    lv_hlp_set_pad_all(o, pad);
    lv_hlp_set_border_none(o);
    lv_obj_set_style_pad_column(o, 0, 0);
    lv_obj_set_style_pad_row(o, 0, 0);
    return o;
}

// Create a label with text, color, and font size.
inline lv_obj_t* lv_hlp_label(lv_obj_t* parent, const char* text,
                                lv_color_t color, int fontSize) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text ? text : "");
    lv_hlp_set_text_color(l, color);
    lv_hlp_set_font(l, lv_hlp_font(fontSize));
    return l;
}

// Create a simple button with a text label child.
inline lv_obj_t* lv_hlp_btn(lv_obj_t* parent, const char* labelText,
                              lv_color_t bg, int fontSize) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_hlp_set_bg(btn, bg);
    lv_hlp_set_border_none(btn);
    lv_hlp_set_radius(btn, 8);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, labelText ? labelText : "");
    lv_hlp_set_font(lbl, lv_hlp_font(fontSize));
    lv_obj_center(lbl);
    return btn;
}

// =============================================================================
// Screen navigation
// LVGL 8: lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT/RIGHT, time, 0, false)
// LVGL 9: lv_screen_load_anim() — update only this function.
// direction: true = slide left (new screen enters from right), false = slide right.
// =============================================================================
inline void lv_hlp_load_screen(lv_obj_t* scr, bool slideLeft, uint32_t time_ms = 300) {
    lv_scr_load_anim(scr,
        slideLeft ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT,
        time_ms, 0, false);
}

inline void lv_hlp_load_screen_instant(lv_obj_t* scr) {
    lv_scr_load(scr);
}
