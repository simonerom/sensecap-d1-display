// =============================================================================
// settings_page.cpp — Settings form (light theme)
// =============================================================================
#include "settings_page.h"
#include "../include/config.h"
#include <Arduino.h>

// Light theme colours
#define L_BG         0xF5F5F5
#define L_CARD       0xFFFFFF
#define L_TEXT       0x1A1A2E
#define L_LABEL      0x555577
#define L_BORDER     0xCCCCDD
#define L_INPUT_BG   0xFFFFFF
#define L_ACCENT     0x5B21B6   // purple accent matching dashboard
#define L_BTN_SAVE   0x5B21B6
#define L_BTN_TZ     0x5B21B6

static const int ROW_H   = 46;
static const int FONT_LBL = 14;
static const int FONT_IN  = 18;
static const int FONT_TIT = 16;

// =============================================================================
// Constructor
// =============================================================================
SettingsPage::SettingsPage()
    : _taSSID(nullptr), _taPassword(nullptr), _taHost(nullptr), _taPort(nullptr),
      _spinboxTZ(nullptr), _btnSave(nullptr), _btnShowPwd(nullptr),
      _kbdPanel(nullptr), _kbdFieldLabel(nullptr), _kbdPreview(nullptr),
      _keyboard(nullptr), _activeTA(nullptr),
      _calScreen(nullptr), _calCrosshair(nullptr), _calInstruction(nullptr),
      _calProgress(nullptr), _calBtnSkip(nullptr), _calStep(0) {}

// =============================================================================
// setCallbacks
// =============================================================================
void SettingsPage::setCallbacks(std::function<void(const AppSettings&)> onSave,
                                 std::function<void(const TouchCalibration&)> onCalDone) {
    _onSave    = onSave;
    _onCalDone = onCalDone;
}

// =============================================================================
// helpers
// =============================================================================
static lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, int y) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_hlp_hex(L_LABEL), 0);
    lv_hlp_set_font(lbl, lv_hlp_font(FONT_LBL));
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, y);
    return lbl;
}

static void styleInput(lv_obj_t* ta, int font) {
    lv_hlp_set_bg(ta, lv_hlp_hex(L_INPUT_BG));
    lv_obj_set_style_text_color(ta, lv_hlp_hex(L_TEXT), 0);
    lv_obj_set_style_border_color(ta, lv_hlp_hex(L_BORDER), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_hlp_set_radius(ta, 6);
    lv_hlp_set_font(ta, lv_hlp_font(font));
    lv_obj_set_style_pad_hor(ta, 10, 0);
    lv_obj_set_style_pad_ver(ta, 8, 0);
}

// =============================================================================
// build
// =============================================================================
void SettingsPage::build(lv_obj_t* parent,
                          std::function<void(const AppSettings&)> onSave,
                          std::function<void(const TouchCalibration&)> onCalDone) {
    if (onSave)    _onSave    = onSave;
    if (onCalDone) _onCalDone = onCalDone;

    int y = 0;

    // ---- Title ----
    lv_obj_t* title = lv_label_create(parent);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS "  Settings");
    lv_obj_set_style_text_color(title, lv_hlp_hex(L_TEXT), 0);
    lv_hlp_set_font(title, lv_hlp_font(FONT_TIT));
    lv_obj_set_style_text_letter_space(title, 1, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, y);
    y += 28;

    // ---- Row 1: SSID + Password ----
    makeLabel(parent, "WiFi SSID", y);
    lv_obj_t* lblP = lv_label_create(parent);
    lv_label_set_text(lblP, "Password");
    lv_obj_set_style_text_color(lblP, lv_hlp_hex(L_LABEL), 0);
    lv_hlp_set_font(lblP, lv_hlp_font(FONT_LBL));
    lv_obj_align(lblP, LV_ALIGN_TOP_LEFT, 222, y);
    y += 18;

    _taSSID = lv_textarea_create(parent);
    lv_obj_set_size(_taSSID, 216, ROW_H);
    lv_obj_align(_taSSID, LV_ALIGN_TOP_LEFT, 0, y);
    lv_textarea_set_one_line(_taSSID, true);
    lv_textarea_set_placeholder_text(_taSSID, "Network name");
    styleInput(_taSSID, FONT_IN);
    lv_obj_add_event_cb(_taSSID, _onTAFocused, LV_EVENT_FOCUSED, this);

    _taPassword = lv_textarea_create(parent);
    lv_obj_set_size(_taPassword, 186, ROW_H);
    lv_obj_align(_taPassword, LV_ALIGN_TOP_LEFT, 222, y);
    lv_textarea_set_one_line(_taPassword, true);
    lv_textarea_set_password_mode(_taPassword, true);
    lv_textarea_set_placeholder_text(_taPassword, "Password");
    styleInput(_taPassword, FONT_IN);
    lv_obj_add_event_cb(_taPassword, _onTAFocused, LV_EVENT_FOCUSED, this);

    // Eye button
    _btnShowPwd = lv_btn_create(parent);
    lv_obj_set_size(_btnShowPwd, ROW_H, ROW_H);
    lv_obj_align(_btnShowPwd, LV_ALIGN_TOP_LEFT, 414, y);
    lv_hlp_set_bg(_btnShowPwd, lv_hlp_hex(L_BORDER));
    lv_obj_set_style_border_width(_btnShowPwd, 1, 0);
    lv_obj_set_style_border_color(_btnShowPwd, lv_hlp_hex(L_BORDER), 0);
    lv_hlp_set_radius(_btnShowPwd, 6);
    lv_hlp_set_pad_all(_btnShowPwd, 4);
    lv_obj_add_event_cb(_btnShowPwd, _onShowPwdClicked, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* eyeIco = lv_label_create(_btnShowPwd);
    lv_label_set_text(eyeIco, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_color(eyeIco, lv_hlp_hex(L_TEXT), 0);
    lv_obj_center(eyeIco);
    y += ROW_H + 12;

    // ---- Row 2: Server IP + Port ----
    makeLabel(parent, "Server IP", y);
    lv_obj_t* lblPort = lv_label_create(parent);
    lv_label_set_text(lblPort, "Port");
    lv_obj_set_style_text_color(lblPort, lv_hlp_hex(L_LABEL), 0);
    lv_hlp_set_font(lblPort, lv_hlp_font(FONT_LBL));
    lv_obj_align(lblPort, LV_ALIGN_TOP_LEFT, 222, y);
    y += 18;

    _taHost = lv_textarea_create(parent);
    lv_obj_set_size(_taHost, 216, ROW_H);
    lv_obj_align(_taHost, LV_ALIGN_TOP_LEFT, 0, y);
    lv_textarea_set_one_line(_taHost, true);
    lv_textarea_set_placeholder_text(_taHost, "192.168.1.100");
    styleInput(_taHost, FONT_IN);
    lv_obj_add_event_cb(_taHost, _onTAFocused, LV_EVENT_FOCUSED, this);

    _taPort = lv_textarea_create(parent);
    lv_obj_set_size(_taPort, 110, ROW_H);
    lv_obj_align(_taPort, LV_ALIGN_TOP_LEFT, 222, y);
    lv_textarea_set_one_line(_taPort, true);
    lv_textarea_set_accepted_chars(_taPort, "0123456789");
    lv_textarea_set_placeholder_text(_taPort, "8765");
    styleInput(_taPort, FONT_IN);
    lv_obj_add_event_cb(_taPort, _onTAFocused, LV_EVENT_FOCUSED, this);
    y += ROW_H + 12;

    // ---- Row 3: Timezone ----
    makeLabel(parent, "Timezone (UTC offset)", y);
    y += 18;

    // [-] spinbox [+]
    int tzBtnW = 50;
    int tzBoxW = 100;
    int tzTotalX = 0;

    lv_obj_t* btnDec = lv_btn_create(parent);
    lv_obj_set_size(btnDec, tzBtnW, ROW_H);
    lv_obj_align(btnDec, LV_ALIGN_TOP_LEFT, tzTotalX, y);
    lv_hlp_set_bg(btnDec, lv_hlp_hex(L_BTN_TZ));
    lv_hlp_set_radius(btnDec, 6);
    lv_obj_add_event_cb(btnDec, _onSpinboxDec, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* lblDec = lv_label_create(btnDec);
    lv_label_set_text(lblDec, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(lblDec, lv_color_white(), 0);
    lv_hlp_set_font(lblDec, lv_hlp_font(18));
    lv_obj_center(lblDec);

    _spinboxTZ = lv_spinbox_create(parent);
    lv_obj_set_size(_spinboxTZ, tzBoxW, ROW_H);
    lv_obj_align(_spinboxTZ, LV_ALIGN_TOP_LEFT, tzTotalX + tzBtnW + 4, y);
    lv_spinbox_set_range(_spinboxTZ, -12, 14);
    lv_spinbox_set_digit_format(_spinboxTZ, 3, 0);
    lv_spinbox_set_value(_spinboxTZ, TIMEZONE_OFFSET_DEFAULT);
    lv_hlp_set_bg(_spinboxTZ, lv_hlp_hex(L_INPUT_BG));
    lv_obj_set_style_text_color(_spinboxTZ, lv_hlp_hex(L_TEXT), 0);
    lv_obj_set_style_border_color(_spinboxTZ, lv_hlp_hex(L_BORDER), 0);
    lv_obj_set_style_border_width(_spinboxTZ, 1, 0);
    lv_hlp_set_radius(_spinboxTZ, 6);
    lv_hlp_set_font(_spinboxTZ, lv_hlp_font(FONT_IN));

    lv_obj_t* btnInc = lv_btn_create(parent);
    lv_obj_set_size(btnInc, tzBtnW, ROW_H);
    lv_obj_align(btnInc, LV_ALIGN_TOP_LEFT, tzTotalX + tzBtnW + 4 + tzBoxW + 4, y);
    lv_hlp_set_bg(btnInc, lv_hlp_hex(L_BTN_TZ));
    lv_hlp_set_radius(btnInc, 6);
    lv_obj_add_event_cb(btnInc, _onSpinboxInc, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* lblInc = lv_label_create(btnInc);
    lv_label_set_text(lblInc, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(lblInc, lv_color_white(), 0);
    lv_hlp_set_font(lblInc, lv_hlp_font(18));
    lv_obj_center(lblInc);
    y += ROW_H + 20;

    // ---- Save button ----
    _btnSave = lv_btn_create(parent);
    lv_obj_set_size(_btnSave, 200, 50);
    lv_obj_align(_btnSave, LV_ALIGN_TOP_RIGHT, 0, y);
    lv_hlp_set_bg(_btnSave, lv_hlp_hex(L_BTN_SAVE));
    lv_hlp_set_radius(_btnSave, 8);
    lv_obj_add_event_cb(_btnSave, _onSaveClicked, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* lblSave = lv_label_create(_btnSave);
    lv_label_set_text(lblSave, LV_SYMBOL_SAVE "  Save & Reboot");
    lv_obj_set_style_text_color(lblSave, lv_color_white(), 0);
    lv_hlp_set_font(lblSave, lv_hlp_font(16));
    lv_obj_center(lblSave);

    // ---- Keyboard overlay ----
    _buildKeyboardPanel(parent);
}

// =============================================================================
// populate
// =============================================================================
void SettingsPage::populate(const AppSettings& s) {
    if (_taSSID)     lv_textarea_set_text(_taSSID,    s.wifiSSID.c_str());
    if (_taPassword) lv_textarea_set_text(_taPassword, s.wifiPassword.c_str());
    if (_taHost)     lv_textarea_set_text(_taHost,    s.serverHost.c_str());
    if (_taPort)     lv_textarea_set_text(_taPort,    String(s.serverPort).c_str());
    if (_spinboxTZ)  lv_spinbox_set_value(_spinboxTZ, s.timezoneOffset);
}

// =============================================================================
// applyCalibration — no-op (calibration removed)
// =============================================================================
void SettingsPage::applyCalibration(const TouchCalibration& cal) {}

// =============================================================================
// startCalibration — no-op
// =============================================================================
void SettingsPage::startCalibration() {}

// =============================================================================
// _buildKeyboardPanel — fullscreen overlay on lv_layer_top()
// =============================================================================
void SettingsPage::_buildKeyboardPanel(lv_obj_t* /*screenParent*/) {
    // Use lv_layer_top() so the panel is fixed above everything, never scrolls
    _kbdPanel = lv_obj_create(lv_layer_top());
    lv_obj_set_size(_kbdPanel, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(_kbdPanel, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_hlp_set_bg(_kbdPanel, lv_hlp_hex(0xF5F5FA));
    lv_hlp_set_border_none(_kbdPanel);
    lv_hlp_set_radius(_kbdPanel, 0);
    lv_obj_set_style_pad_all(_kbdPanel, 12, 0);
    lv_hlp_no_scroll(_kbdPanel);

    // Field name label (top-left)
    _kbdFieldLabel = lv_label_create(_kbdPanel);
    lv_label_set_text(_kbdFieldLabel, "");
    lv_obj_set_style_text_color(_kbdFieldLabel, lv_hlp_hex(L_LABEL), 0);
    lv_hlp_set_font(_kbdFieldLabel, lv_hlp_font(13));
    lv_obj_align(_kbdFieldLabel, LV_ALIGN_TOP_LEFT, 0, 0);

    // Preview textarea (full width, below label)
    _kbdPreview = lv_textarea_create(_kbdPanel);
    lv_obj_set_size(_kbdPreview, SCREEN_WIDTH - 24, 54);
    lv_obj_align(_kbdPreview, LV_ALIGN_TOP_MID, 0, 22);
    lv_textarea_set_one_line(_kbdPreview, true);
    lv_hlp_set_font(_kbdPreview, lv_hlp_font(24));
    lv_hlp_set_bg(_kbdPreview, lv_hlp_hex(L_INPUT_BG));
    lv_obj_set_style_text_color(_kbdPreview, lv_hlp_hex(L_TEXT), 0);
    lv_obj_set_style_border_color(_kbdPreview, lv_hlp_hex(L_ACCENT), 0);
    lv_obj_set_style_border_width(_kbdPreview, 2, 0);
    lv_hlp_set_radius(_kbdPreview, 8);
    lv_obj_set_style_pad_hor(_kbdPreview, 12, 0);
    lv_obj_set_style_pad_ver(_kbdPreview, 10, 0);

    // Keyboard — fills the rest of the screen below preview
    _keyboard = lv_keyboard_create(_kbdPanel);
    lv_obj_set_size(_keyboard, SCREEN_WIDTH - 24, SCREEN_HEIGHT - 100);
    lv_obj_align(_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_hlp_set_bg(_keyboard, lv_hlp_hex(0xF5F5FA));
    lv_keyboard_set_textarea(_keyboard, _kbdPreview);
    lv_obj_add_event_cb(_keyboard, _onKeyboardReady, LV_EVENT_READY,  this);
    lv_obj_add_event_cb(_keyboard, _onKeyboardReady, LV_EVENT_CANCEL, this);

    lv_obj_add_flag(_kbdPanel, LV_OBJ_FLAG_HIDDEN);
}

// =============================================================================
// _buildCalibrationScreen — stub (removed)
// =============================================================================
void SettingsPage::_buildCalibrationScreen(lv_obj_t*) {}
void SettingsPage::_advanceCalStep() {}
void SettingsPage::_finishCalibration() {}

// =============================================================================
// _showKeyboard / _hideKeyboard
// =============================================================================
void SettingsPage::_showKeyboard(lv_obj_t* ta) {
    if (!ta || !_kbdPanel) return;
    _activeTA = ta;
    const char* fieldName = "Input";
    if (ta == _taSSID)          fieldName = "WiFi SSID";
    else if (ta == _taPassword) fieldName = "Password";
    else if (ta == _taHost)     fieldName = "Server IP";
    else if (ta == _taPort)     fieldName = "Port";
    lv_label_set_text(_kbdFieldLabel, fieldName);
    lv_textarea_set_text(_kbdPreview, lv_textarea_get_text(ta));
    bool isPwd = (ta == _taPassword) && lv_textarea_get_password_mode(ta);
    lv_textarea_set_password_mode(_kbdPreview, isPwd);
    lv_keyboard_set_textarea(_keyboard, _kbdPreview);
    lv_obj_clear_flag(_kbdPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_kbdPanel);
}

void SettingsPage::_hideKeyboard() {
    if (!_kbdPanel) return;
    if (_activeTA && _kbdPreview)
        lv_textarea_set_text(_activeTA, lv_textarea_get_text(_kbdPreview));
    lv_obj_add_flag(_kbdPanel, LV_OBJ_FLAG_HIDDEN);
    _activeTA = nullptr;
}

// =============================================================================
// Static callbacks
// =============================================================================
void SettingsPage::_onTAFocused(lv_event_t* e) {
    SettingsPage* self = (SettingsPage*)lv_event_get_user_data(e);
    if (self) self->_showKeyboard(lv_event_get_target(e));
}
void SettingsPage::_onKeyboardReady(lv_event_t* e) {
    SettingsPage* self = (SettingsPage*)lv_event_get_user_data(e);
    if (self) self->_hideKeyboard();
}
void SettingsPage::_onSaveClicked(lv_event_t* e) {
    SettingsPage* self = (SettingsPage*)lv_event_get_user_data(e);
    if (!self) return;
    self->_hideKeyboard();
    AppSettings s;
    s.wifiSSID      = lv_textarea_get_text(self->_taSSID);
    s.wifiPassword  = lv_textarea_get_text(self->_taPassword);
    s.serverHost    = lv_textarea_get_text(self->_taHost);
    s.serverPort    = (uint16_t)String(lv_textarea_get_text(self->_taPort)).toInt();
    if (s.serverPort == 0) s.serverPort = DATA_ENDPOINT_PORT_DEFAULT;
    s.timezoneOffset = (int8_t)lv_spinbox_get_value(self->_spinboxTZ);
    s.configured    = true;
    if (self->_onSave) self->_onSave(s);
}
void SettingsPage::_onSpinboxInc(lv_event_t* e) {
    SettingsPage* self = (SettingsPage*)lv_event_get_user_data(e);
    if (self && self->_spinboxTZ) lv_spinbox_increment(self->_spinboxTZ);
}
void SettingsPage::_onSpinboxDec(lv_event_t* e) {
    SettingsPage* self = (SettingsPage*)lv_event_get_user_data(e);
    if (self && self->_spinboxTZ) lv_spinbox_decrement(self->_spinboxTZ);
}
void SettingsPage::_onShowPwdClicked(lv_event_t* e) {
    SettingsPage* self = (SettingsPage*)lv_event_get_user_data(e);
    if (!self || !self->_taPassword || !self->_btnShowPwd) return;
    bool hidden = lv_textarea_get_password_mode(self->_taPassword);
    lv_textarea_set_password_mode(self->_taPassword, !hidden);
    lv_obj_t* ico = lv_obj_get_child(self->_btnShowPwd, 0);
    if (ico) lv_label_set_text(ico, hidden ? LV_SYMBOL_EYE_CLOSE : LV_SYMBOL_EYE_OPEN);
}
void SettingsPage::_onCalibrateClicked(lv_event_t*) {}
void SettingsPage::_onCalTap(lv_event_t*) {}
void SettingsPage::_onCalSkip(lv_event_t*) {}
