// =============================================================================
// settings_page.cpp — Settings form + touch calibration
// =============================================================================
#include "settings_page.h"
#include "../include/config.h"
#include <Arduino.h>

// Forward-declare the global touch calibration state from ui.cpp
// (the actual variables live there alongside the touch driver)
extern int16_t _cal_x0, _cal_x1, _cal_y0, _cal_y1;
extern bool    _cal_valid;
// Raw-only read used during calibration procedure
extern bool ft5x06_read_touch(uint16_t* x, uint16_t* y);

// =============================================================================
// Calibration target positions (screen coordinates)
// =============================================================================
static const struct { int16_t x; int16_t y; } CAL_TARGETS[CAL_POINT_COUNT] = {
    {  30,  30  },   // 0: Top-Left
    { 450,  30  },   // 1: Top-Right
    {  30, 450  },   // 2: Bottom-Left
    { 450, 450  },   // 3: Bottom-Right
    { 240, 240  },   // 4: Centre
};
static const char* CAL_LABELS[CAL_POINT_COUNT] = {
    "Top-Left", "Top-Right", "Bottom-Left", "Bottom-Right", "Centre"
};

// =============================================================================
// Constructor
// =============================================================================
SettingsPage::SettingsPage()
    : _taSSID(nullptr), _taPassword(nullptr), _taHost(nullptr), _taPort(nullptr),
      _spinboxTZ(nullptr), _btnSave(nullptr), _btnShowPwd(nullptr),
      _kbdPanel(nullptr), _kbdFieldLabel(nullptr), _kbdPreview(nullptr),
      _keyboard(nullptr), _activeTA(nullptr),
      _calScreen(nullptr), _calCrosshair(nullptr), _calInstruction(nullptr),
      _calProgress(nullptr), _calBtnSkip(nullptr), _calStep(0) {
    for (int i = 0; i < CAL_POINT_COUNT; i++) { _calRawX[i] = 0; _calRawY[i] = 0; }
}

// =============================================================================
// setCallbacks — store callbacks before build()
// =============================================================================
void SettingsPage::setCallbacks(std::function<void(const AppSettings&)> onSave,
                                 std::function<void(const TouchCalibration&)> onCalDone) {
    _onSave    = onSave;
    _onCalDone = onCalDone;
}

// =============================================================================
// build — create all settings widgets inside parent
// =============================================================================
void SettingsPage::build(lv_obj_t* parent,
                          std::function<void(const AppSettings&)> onSave,
                          std::function<void(const TouchCalibration&)> onCalDone) {
    if (onSave)    _onSave    = onSave;
    if (onCalDone) _onCalDone = onCalDone;

    // Title
    lv_obj_t* title = lv_hlp_label(parent, LV_SYMBOL_SETTINGS "  SETTINGS",
                                    lv_hlp_hex(COLOR_TEXT_PRIMARY), 14);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    // ---- WiFi SSID ----
    lv_obj_t* lblSSID = lv_hlp_label(parent, "WiFi SSID", lv_hlp_hex(COLOR_TEXT_SECONDARY), 12);
    lv_obj_align(lblSSID, LV_ALIGN_TOP_LEFT, 0, 22);

    _taSSID = lv_textarea_create(parent);
    lv_obj_set_size(_taSSID, 200, 34);
    lv_obj_align(_taSSID, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_textarea_set_one_line(_taSSID, true);
    lv_textarea_set_placeholder_text(_taSSID, "Network name");
    lv_hlp_set_font(_taSSID, lv_hlp_font(14));
    lv_obj_add_event_cb(_taSSID, _onTAFocused, LV_EVENT_FOCUSED, this);

    // ---- WiFi Password ----
    lv_obj_t* lblPass = lv_hlp_label(parent, "Password", lv_hlp_hex(COLOR_TEXT_SECONDARY), 12);
    lv_obj_align(lblPass, LV_ALIGN_TOP_LEFT, 216, 22);

    _taPassword = lv_textarea_create(parent);
    lv_obj_set_size(_taPassword, 206, 34);
    lv_obj_align(_taPassword, LV_ALIGN_TOP_LEFT, 216, 36);
    lv_textarea_set_one_line(_taPassword, true);
    lv_textarea_set_password_mode(_taPassword, true);
    lv_textarea_set_placeholder_text(_taPassword, "Password");
    lv_hlp_set_font(_taPassword, lv_hlp_font(14));
    lv_obj_add_event_cb(_taPassword, _onTAFocused, LV_EVENT_FOCUSED, this);

    // Eye button: toggle password visibility
    _btnShowPwd = lv_btn_create(parent);
    lv_obj_set_size(_btnShowPwd, 30, 34);
    lv_obj_align(_btnShowPwd, LV_ALIGN_TOP_LEFT, 424, 36);
    lv_hlp_set_bg(_btnShowPwd, lv_hlp_hex(0x333355));
    lv_hlp_set_border_none(_btnShowPwd);
    lv_hlp_set_radius(_btnShowPwd, 4);
    lv_hlp_set_pad_all(_btnShowPwd, 4);
    lv_obj_add_event_cb(_btnShowPwd, _onShowPwdClicked, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* eyeIco = lv_label_create(_btnShowPwd);
    lv_label_set_text(eyeIco, LV_SYMBOL_EYE_OPEN);
    lv_hlp_set_text_color(eyeIco, lv_hlp_hex(COLOR_TEXT_SECONDARY));
    lv_obj_center(eyeIco);

    // ---- Server Host ----
    lv_obj_t* lblHost = lv_hlp_label(parent, "Server IP", lv_hlp_hex(COLOR_TEXT_SECONDARY), 12);
    lv_obj_align(lblHost, LV_ALIGN_TOP_LEFT, 0, 78);

    _taHost = lv_textarea_create(parent);
    lv_obj_set_size(_taHost, 200, 34);
    lv_obj_align(_taHost, LV_ALIGN_TOP_LEFT, 0, 92);
    lv_textarea_set_one_line(_taHost, true);
    lv_textarea_set_placeholder_text(_taHost, "192.168.1.100");
    lv_hlp_set_font(_taHost, lv_hlp_font(14));
    lv_obj_add_event_cb(_taHost, _onTAFocused, LV_EVENT_FOCUSED, this);

    // ---- Server Port ----
    lv_obj_t* lblPort = lv_hlp_label(parent, "Port", lv_hlp_hex(COLOR_TEXT_SECONDARY), 12);
    lv_obj_align(lblPort, LV_ALIGN_TOP_LEFT, 216, 78);

    _taPort = lv_textarea_create(parent);
    lv_obj_set_size(_taPort, 100, 34);
    lv_obj_align(_taPort, LV_ALIGN_TOP_LEFT, 216, 92);
    lv_textarea_set_one_line(_taPort, true);
    lv_textarea_set_accepted_chars(_taPort, "0123456789");
    lv_textarea_set_placeholder_text(_taPort, "8765");
    lv_hlp_set_font(_taPort, lv_hlp_font(14));
    lv_obj_add_event_cb(_taPort, _onTAFocused, LV_EVENT_FOCUSED, this);

    // ---- Timezone ----
    lv_obj_t* lblTZ = lv_hlp_label(parent, "Timezone (UTC)", lv_hlp_hex(COLOR_TEXT_SECONDARY), 12);
    lv_obj_align(lblTZ, LV_ALIGN_TOP_LEFT, 332, 78);

    _spinboxTZ = lv_spinbox_create(parent);
    lv_obj_set_size(_spinboxTZ, 100, 34);
    lv_obj_align(_spinboxTZ, LV_ALIGN_TOP_LEFT, 332, 92);
    lv_spinbox_set_range(_spinboxTZ, -12, 14);
    lv_spinbox_set_digit_format(_spinboxTZ, 3, 0);
    lv_spinbox_set_value(_spinboxTZ, TIMEZONE_OFFSET_DEFAULT);
    lv_hlp_set_font(_spinboxTZ, lv_hlp_font(14));

    lv_obj_t* btnInc = lv_btn_create(parent);
    lv_obj_set_size(btnInc, 28, 28);
    lv_obj_align_to(btnInc, _spinboxTZ, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    lv_hlp_set_bg(btnInc, lv_hlp_hex(COLOR_ACCENT));
    lv_obj_add_event_cb(btnInc, _onSpinboxInc, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* lblInc = lv_label_create(btnInc);
    lv_label_set_text(lblInc, LV_SYMBOL_PLUS);
    lv_obj_center(lblInc);

    lv_obj_t* btnDec = lv_btn_create(parent);
    lv_obj_set_size(btnDec, 28, 28);
    lv_obj_align_to(btnDec, _spinboxTZ, LV_ALIGN_OUT_LEFT_MID, -4, 0);
    lv_hlp_set_bg(btnDec, lv_hlp_hex(COLOR_ACCENT));
    lv_obj_add_event_cb(btnDec, _onSpinboxDec, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* lblDec = lv_label_create(btnDec);
    lv_label_set_text(lblDec, LV_SYMBOL_MINUS);
    lv_obj_center(lblDec);

    // ---- Save button ----
    _btnSave = lv_btn_create(parent);
    lv_obj_set_size(_btnSave, 160, 40);
    lv_obj_align(_btnSave, LV_ALIGN_TOP_RIGHT, 0, 135);
    lv_hlp_set_bg(_btnSave, lv_hlp_hex(COLOR_ACCENT));
    lv_obj_add_event_cb(_btnSave, _onSaveClicked, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* lblSave = lv_label_create(_btnSave);
    lv_label_set_text(lblSave, LV_SYMBOL_SAVE "  Save & Reboot");
    lv_hlp_set_font(lblSave, lv_hlp_font(14));
    lv_obj_center(lblSave);

    // ---- Calibrate Touch button ----
    lv_obj_t* btnCal = lv_btn_create(parent);
    lv_obj_set_size(btnCal, 160, 40);
    lv_obj_align(btnCal, LV_ALIGN_TOP_LEFT, 0, 135);
    lv_hlp_set_bg(btnCal, lv_hlp_hex(0x334466));
    lv_obj_add_event_cb(btnCal, _onCalibrateClicked, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* lblCal = lv_label_create(btnCal);
    lv_label_set_text(lblCal, LV_SYMBOL_EDIT "  Calibrate Touch");
    lv_hlp_set_font(lblCal, lv_hlp_font(14));
    lv_obj_center(lblCal);

    // ---- Keyboard panel and calibration screen (overlays on same screen) ----
    _buildKeyboardPanel(parent);
    _buildCalibrationScreen(parent);
}

// =============================================================================
// populate — pre-fill form fields from saved settings
// =============================================================================
void SettingsPage::populate(const AppSettings& s) {
    if (_taSSID)     lv_textarea_set_text(_taSSID,    s.wifiSSID.c_str());
    if (_taPassword) lv_textarea_set_text(_taPassword, s.wifiPassword.c_str());
    if (_taHost)     lv_textarea_set_text(_taHost,    s.serverHost.c_str());
    if (_taPort)     lv_textarea_set_text(_taPort,    String(s.serverPort).c_str());
    if (_spinboxTZ)  lv_spinbox_set_value(_spinboxTZ, s.timezoneOffset);
}

// =============================================================================
// applyCalibration — update global touch calibration coefficients
// =============================================================================
void SettingsPage::applyCalibration(const TouchCalibration& cal) {
    if (!cal.valid) return;
    _cal_x0 = cal.x0;
    _cal_x1 = cal.x1;
    _cal_y0 = cal.y0;
    _cal_y1 = cal.y1;
    _cal_valid = true;
}

// =============================================================================
// _buildKeyboardPanel — fullscreen keyboard overlay
// =============================================================================
void SettingsPage::_buildKeyboardPanel(lv_obj_t* screenParent) {
    _kbdPanel = lv_obj_create(screenParent);
    lv_obj_set_size(_kbdPanel, SCREEN_WIDTH, 336);
    lv_obj_align(_kbdPanel, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_hlp_set_bg(_kbdPanel, lv_hlp_hex(0x12122A));
    lv_hlp_set_border_none(_kbdPanel);
    lv_hlp_set_radius(_kbdPanel, 0);
    lv_hlp_set_pad_all(_kbdPanel, 6);
    lv_hlp_no_scroll(_kbdPanel);

    _kbdFieldLabel = lv_hlp_label(_kbdPanel, "", lv_hlp_hex(COLOR_TEXT_SECONDARY), 12);
    lv_obj_align(_kbdFieldLabel, LV_ALIGN_TOP_LEFT, 4, 0);

    _kbdPreview = lv_textarea_create(_kbdPanel);
    lv_obj_set_size(_kbdPreview, SCREEN_WIDTH - 12, 48);
    lv_obj_align(_kbdPreview, LV_ALIGN_TOP_MID, 0, 18);
    lv_textarea_set_one_line(_kbdPreview, true);
    lv_hlp_set_font(_kbdPreview, lv_hlp_font(22));
    lv_hlp_set_bg(_kbdPreview, lv_hlp_hex(0x1E1E3A));
    lv_obj_set_style_border_color(_kbdPreview, lv_hlp_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_border_width(_kbdPreview, 2, 0);
    lv_hlp_set_radius(_kbdPreview, 6);

    _keyboard = lv_keyboard_create(_kbdPanel);
    lv_obj_set_size(_keyboard, SCREEN_WIDTH - 12, 262);
    lv_obj_align(_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_hlp_set_bg(_keyboard, lv_hlp_hex(0x12122A));
    lv_keyboard_set_textarea(_keyboard, _kbdPreview);
    lv_obj_add_event_cb(_keyboard, _onKeyboardReady, LV_EVENT_READY,  this);
    lv_obj_add_event_cb(_keyboard, _onKeyboardReady, LV_EVENT_CANCEL, this);

    lv_obj_add_flag(_kbdPanel, LV_OBJ_FLAG_HIDDEN);
}

// =============================================================================
// _buildCalibrationScreen — fullscreen calibration overlay
// =============================================================================
void SettingsPage::_buildCalibrationScreen(lv_obj_t* screenParent) {
    _calScreen = lv_obj_create(screenParent);
    lv_obj_set_size(_calScreen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(_calScreen, LV_ALIGN_CENTER, 0, 0);
    lv_hlp_set_bg(_calScreen, lv_color_black());
    lv_hlp_set_border_none(_calScreen);
    lv_hlp_set_radius(_calScreen, 0);
    lv_hlp_no_scroll(_calScreen);
    lv_obj_add_flag(_calScreen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_calScreen, _onCalTap, LV_EVENT_SHORT_CLICKED, this);

    lv_obj_t* title = lv_hlp_label(_calScreen, "TOUCH CALIBRATION",
                                     lv_color_white(), 14);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    _calProgress = lv_hlp_label(_calScreen, "Step 1 / 5",
                                  lv_hlp_hex(COLOR_TEXT_SECONDARY), 12);
    lv_obj_align(_calProgress, LV_ALIGN_TOP_MID, 0, 28);

    _calInstruction = lv_hlp_label(_calScreen, "Tap the crosshair",
                                    lv_hlp_hex(COLOR_TEXT_PRIMARY), 18);
    lv_obj_align(_calInstruction, LV_ALIGN_BOTTOM_MID, 0, -56);

    _calCrosshair = lv_hlp_label(_calScreen, "+", lv_hlp_hex(COLOR_ACCENT), 48);
    lv_obj_align(_calCrosshair, LV_ALIGN_TOP_LEFT,
                 CAL_TARGETS[0].x - 20, CAL_TARGETS[0].y - 30);

    _calBtnSkip = lv_btn_create(_calScreen);
    lv_obj_set_size(_calBtnSkip, 120, 36);
    lv_obj_align(_calBtnSkip, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_hlp_set_bg(_calBtnSkip, lv_hlp_hex(0x555555));
    lv_obj_add_event_cb(_calBtnSkip, _onCalSkip, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* lblSkip = lv_label_create(_calBtnSkip);
    lv_label_set_text(lblSkip, "Skip");
    lv_hlp_set_font(lblSkip, lv_hlp_font(14));
    lv_obj_center(lblSkip);

    lv_obj_add_flag(_calScreen, LV_OBJ_FLAG_HIDDEN);
}

// =============================================================================
// startCalibration
// =============================================================================
void SettingsPage::startCalibration() {
    if (!_calScreen) return;
    _calStep = 0;
    _cal_valid = false;  // disable calibration during procedure

    char buf[24];
    snprintf(buf, sizeof(buf), "Step 1 / %d", CAL_POINT_COUNT);
    lv_label_set_text(_calProgress, buf);
    lv_label_set_text(_calInstruction, CAL_LABELS[0]);
    lv_obj_align(_calCrosshair, LV_ALIGN_TOP_LEFT,
                 CAL_TARGETS[0].x - 20, CAL_TARGETS[0].y - 30);

    lv_obj_clear_flag(_calScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_calScreen);
}

// =============================================================================
// _advanceCalStep
// =============================================================================
void SettingsPage::_advanceCalStep() {
    _calStep++;
    if (_calStep >= CAL_POINT_COUNT) {
        _finishCalibration();
        return;
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "Step %d / %d", _calStep + 1, CAL_POINT_COUNT);
    lv_label_set_text(_calProgress, buf);
    lv_label_set_text(_calInstruction, CAL_LABELS[_calStep]);
    lv_obj_align(_calCrosshair, LV_ALIGN_TOP_LEFT,
                 CAL_TARGETS[_calStep].x - 20, CAL_TARGETS[_calStep].y - 30);
}

// =============================================================================
// _finishCalibration — compute + apply + persist
// =============================================================================
void SettingsPage::_finishCalibration() {
    int16_t tl_rx = _calRawX[0], tl_ry = _calRawY[0];
    int16_t br_rx = _calRawX[3], br_ry = _calRawY[3];

    if (br_rx == tl_rx || br_ry == tl_ry) {
        Serial.println("[Cal] Invalid data, skipping.");
        lv_obj_add_flag(_calScreen, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    int32_t range_px_x = CAL_TARGETS[3].x - CAL_TARGETS[0].x;
    int32_t range_px_y = CAL_TARGETS[3].y - CAL_TARGETS[0].y;
    int32_t range_raw_x = br_rx - tl_rx;
    int32_t range_raw_y = br_ry - tl_ry;

    TouchCalibration cal;
    cal.x0 = (int16_t)(tl_rx - (int32_t)CAL_TARGETS[0].x * range_raw_x / range_px_x);
    cal.x1 = (int16_t)(cal.x0 + (SCREEN_WIDTH  - 1) * range_raw_x / range_px_x);
    cal.y0 = (int16_t)(tl_ry - (int32_t)CAL_TARGETS[0].y * range_raw_y / range_px_y);
    cal.y1 = (int16_t)(cal.y0 + (SCREEN_HEIGHT - 1) * range_raw_y / range_px_y);
    cal.valid = true;

    Serial.printf("[Cal] Done: x=[%d..%d] y=[%d..%d]\n", cal.x0, cal.x1, cal.y0, cal.y1);

    _cal_x0 = cal.x0; _cal_x1 = cal.x1;
    _cal_y0 = cal.y0; _cal_y1 = cal.y1;
    _cal_valid = true;

    if (_onCalDone) _onCalDone(cal);
    lv_obj_add_flag(_calScreen, LV_OBJ_FLAG_HIDDEN);
}

// =============================================================================
// _showKeyboard / _hideKeyboard
// =============================================================================
void SettingsPage::_showKeyboard(lv_obj_t* ta) {
    if (!ta || !_kbdPanel) return;
    _activeTA = ta;

    const char* fieldName = "Input";
    if (ta == _taSSID)     fieldName = "WiFi SSID";
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

void SettingsPage::_onCalibrateClicked(lv_event_t* e) {
    SettingsPage* self = (SettingsPage*)lv_event_get_user_data(e);
    if (self) self->startCalibration();
}

void SettingsPage::_onCalTap(lv_event_t* e) {
    SettingsPage* self = (SettingsPage*)lv_event_get_user_data(e);
    if (!self || self->_calStep >= CAL_POINT_COUNT) return;

    uint16_t x, y;
    if (!ft5x06_read_touch(&x, &y)) {
        lv_indev_t* indev = lv_indev_get_act();
        if (!indev) return;
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);
        x = pt.x; y = pt.y;
    }

    int step = self->_calStep;
    self->_calRawX[step] = (int16_t)x;
    self->_calRawY[step] = (int16_t)y;
    Serial.printf("[Cal] Step %d (%s): x=%d y=%d\n", step, CAL_LABELS[step], x, y);
    self->_advanceCalStep();
}

void SettingsPage::_onCalSkip(lv_event_t* e) {
    SettingsPage* self = (SettingsPage*)lv_event_get_user_data(e);
    if (!self || !self->_calScreen) return;
    _cal_valid = false;
    lv_obj_add_flag(self->_calScreen, LV_OBJ_FLAG_HIDDEN);
    Serial.println("[Cal] Skipped.");
}
