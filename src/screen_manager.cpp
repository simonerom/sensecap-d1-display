// =============================================================================
// screen_manager.cpp
// =============================================================================
#include "screen_manager.h"
#include <Arduino.h>
#include <esp_heap_caps.h>

// Touch indev is registered in ui.cpp; we keep a global pointer set there.
extern lv_indev_t* g_touch_indev;
// Touch pressed state — updated by touch_read_cb in ui.cpp (replaces LVGL9 lv_indev_get_state)
extern bool g_touch_pressed;

// =============================================================================
// Constructor
// =============================================================================
ScreenManager::ScreenManager()
    : _factory(_engine), _parser(_factory) {
    _queue = xQueueCreate(12, sizeof(UiCommand));
}

// =============================================================================
// init
// =============================================================================
void ScreenManager::init(std::function<void(const AppSettings&)>      onSettingsSaved,
                          std::function<void(const TouchCalibration&)> onCalDone) {
    _onSettingsSaved = onSettingsSaved;
    _onCalDone       = onCalDone;

    // Store callbacks; actual build happens in buildFallback() or WidgetFactory
    _settingsPage.setCallbacks(
        [this](const AppSettings& s) { if (_onSettingsSaved) _onSettingsSaved(s); },
        [this](const TouchCalibration& c) { if (_onCalDone) _onCalDone(c); }
    );
    _factory.setSettingsPage(&_settingsPage);

    // Create overlay (parented to lv_layer_top() so it's always above all screens)
    _createOverlay();

    // Save touch indev for gesture detection
    _touchIndev = g_touch_indev;

    // RTC timers (LVGL timers run on UI task)
    _rtcTimer    = lv_timer_create(_rtcTimerCb,    1000, this);
    _minuteTimer = lv_timer_create(_minuteTimerCb, 60000, this);
    _secondTimer = lv_timer_create(_secondTimerCb, 1000, this);

    // Trigger immediately to populate time placeholders before first screen shows
    _engine.updateRtc(_tzOffset);
}

// =============================================================================
// buildFromXml
// =============================================================================
bool ScreenManager::buildFromXml(const char* xml, size_t len) {
    if (!xml) return false;

    // Load a blank screen first so the active screen is not one we're about to delete
    lv_obj_t* blank = lv_obj_create(nullptr);
    lv_hlp_set_bg(blank, lv_hlp_hex(0x000000));
    lv_hlp_load_screen_instant(blank);

    // Now safe to delete old screens
    for (int i = 0; i < 4; i++) {
        if (_screens[i]) {
            lv_obj_del(_screens[i]);
            _screens[i] = nullptr;
        }
    }
    _engine.clearRegistrations();

    // Parse XML and build screens
    ScreenMap built;
    bool ok = _parser.parse(xml, len, built);

    if (!ok) {
        Serial.printf("[SM] XML parse failed: %s\n", _parser.lastError().c_str());
        buildFallback(_parser.lastError().c_str());
        return false;
    }

    // Map screen ids to PageId indices
    const char* idMap[4] = { "settings", "home", "calendar", "clock" };
    for (int i = 0; i < 4; i++) {
        auto it = built.find(idMap[i]);
        if (it != built.end()) {
            _screens[i] = it->second;
        } else {
            Serial.printf("[SM] Screen '%s' not found in XML, using placeholder.\n", idMap[i]);
            lv_obj_t* scr = lv_obj_create(nullptr);
            lv_hlp_set_bg(scr, lv_hlp_hex(0x1A1A2E));
            lv_hlp_set_border_none(scr);
            lv_obj_t* lbl = lv_hlp_label(scr, idMap[i], lv_color_white(), 18);
            lv_obj_center(lbl);
            _screens[i] = scr;
        }
    }

    // The settings screen is handled by SettingsPage which needs to re-build
    // into the screen object returned by the parser. Since <settings_form/> calls
    // SettingsPage::build() inside WidgetFactory, the form was already built.
    // We only need to ensure the keyboard/calibration overlays are correct.
    // They are created in _buildKeyboardPanel() with lv_scr_act() as parent —
    // that's fine because we always navigate to settings via lv_scr_load().

    _screensBuilt = true;

    // Show home screen (or keep current if already visible)
    _navigateTo(PageId::Home, false);

    Serial.println("[SM] Screens built from XML.");
    return true;
}

// =============================================================================
// buildFallback — minimal UI when XML is not available
// =============================================================================
void ScreenManager::buildFallback(const char* errorMsg) {
    for (int i = 0; i < 4; i++) {
        if (_screens[i]) { lv_obj_del(_screens[i]); _screens[i] = nullptr; }
    }

    // Settings screen with real form
    lv_obj_t* settingsScr = lv_obj_create(nullptr);
    lv_hlp_set_bg(settingsScr, lv_hlp_hex(0x1A1A2E));
    lv_hlp_set_border_none(settingsScr);
    lv_hlp_set_pad_all(settingsScr, 12);
    lv_hlp_flex_col(settingsScr, 8);
    _settingsPage.build(settingsScr,
        [this](const AppSettings& s) { if (_onSettingsSaved) _onSettingsSaved(s); },
        [this](const TouchCalibration& c) { if (_onCalDone) _onCalDone(c); }
    );
    _screens[(int)PageId::Settings] = settingsScr;

    // Minimal screens for other pages
    const char* labels[4] = { nullptr, "No layout", "No layout", "No layout" };
    for (int i = 1; i < 4; i++) {
        lv_obj_t* scr = lv_obj_create(nullptr);
        lv_hlp_set_bg(scr, lv_hlp_hex(0x1A1A2E));
        lv_hlp_set_border_none(scr);
        if (errorMsg && i == 1) {
            lv_obj_t* lbl = lv_hlp_label(scr, errorMsg, lv_hlp_hex(0xFF4444), 14);
            lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
            lv_obj_set_width(lbl, LV_PCT(90));
            lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        } else {
            lv_obj_t* lbl = lv_hlp_label(scr, labels[i], lv_color_white(), 18);
            lv_obj_center(lbl);
        }
        _screens[i] = scr;
    }

    _screensBuilt = true;
    _navigateTo(PageId::Home, false);
}

// =============================================================================
// Navigation
// =============================================================================
void ScreenManager::_navigateTo(PageId page, bool animate) {
    int idx = (int)page;
    if (!_screens[idx]) return;

    int cur = (int)_currentPage;
    bool goRight = idx > cur;  // new page is to the right

    if (animate && _screensBuilt) {
        // slideLeft=true means new screen enters from right (we move right)
        lv_hlp_load_screen(_screens[idx], goRight);
    } else {
        lv_hlp_load_screen_instant(_screens[idx]);
    }
    _currentPage = page;
}

void ScreenManager::goTo(PageId page) {
    _navigateTo(page, true);
}

void ScreenManager::goToSettings() {
    _navigateTo(PageId::Settings, true);
}

// =============================================================================
// Thread-safe post methods
// =============================================================================
void ScreenManager::postDataUpdate(DataPayload* payload) {
    UiCommand cmd;
    cmd.type    = UiCmdType::DataUpdate;
    cmd.dataPtr = payload;
    xQueueSend(_queue, &cmd, 0);
}

void ScreenManager::postSensorUpdate(float temp, float hum, bool ok, float tvoc, float co2) {
    UiCommand cmd;
    cmd.type = UiCmdType::SensorUpdate;
    cmd.f0 = temp; cmd.f1 = hum; cmd.b0 = ok;
    cmd.f2 = tvoc; cmd.f3 = co2;
    xQueueSend(_queue, &cmd, 0);
}

void ScreenManager::postRebuildLayout(char* xml, size_t len) {
    UiCommand cmd;
    cmd.type   = UiCmdType::RebuildLayout;
    cmd.xmlPtr = xml;
    cmd.xmlLen = len;
    xQueueSend(_queue, &cmd, 0);
}

void ScreenManager::postShowConnecting(const String& ssid) {
    UiCommand cmd;
    cmd.type = UiCmdType::ShowConnecting;
    strncpy(cmd.str, ssid.c_str(), sizeof(cmd.str) - 1);
    xQueueSend(_queue, &cmd, 0);
}

void ScreenManager::postShowError(const String& msg) {
    UiCommand cmd;
    cmd.type = UiCmdType::ShowError;
    strncpy(cmd.str, msg.c_str(), sizeof(cmd.str) - 1);
    xQueueSend(_queue, &cmd, 0);
}

void ScreenManager::postHideOverlay() {
    UiCommand cmd; cmd.type = UiCmdType::HideOverlay;
    xQueueSend(_queue, &cmd, 0);
}

void ScreenManager::postGoToSettings() {
    UiCommand cmd; cmd.type = UiCmdType::GoToSettings;
    xQueueSend(_queue, &cmd, 0);
}

bool ScreenManager::consumeRefreshRequest() {
    if (_refreshRequested) { _refreshRequested = false; return true; }
    return false;
}

void ScreenManager::applyCalibration(const TouchCalibration& cal) {
    _settingsPage.applyCalibration(cal);
}

void ScreenManager::populateSettings(const AppSettings& s) {
    _settingsPage.populate(s);
    _tzOffset = s.timezoneOffset;
}

// =============================================================================
// tick — called every 5 ms from taskUI
// =============================================================================
void ScreenManager::tick() {
    lv_timer_handler();
    _processQueue();
    _processGesture();
}

// =============================================================================
// _processQueue
// =============================================================================
void ScreenManager::_processQueue() {
    UiCommand cmd;
    while (xQueueReceive(_queue, &cmd, 0) == pdTRUE) {
        switch (cmd.type) {
        case UiCmdType::DataUpdate:
            if (cmd.dataPtr) {
                _engine.applyData(cmd.dataPtr->scalars, cmd.dataPtr->arrays);
                delete cmd.dataPtr;
            }
            _hideRefreshSpinner();
            _hideOverlay();
            break;

        case UiCmdType::SensorUpdate:
            _engine.updateSensor(cmd.f0, cmd.f1, cmd.b0, cmd.f2, cmd.f3);
            break;

        case UiCmdType::RebuildLayout:
            if (cmd.xmlPtr) {
                buildFromXml(cmd.xmlPtr, cmd.xmlLen);
                heap_caps_free(cmd.xmlPtr);
            }
            break;

        case UiCmdType::ShowConnecting:
            _showConnecting(cmd.str);
            break;

        case UiCmdType::ShowError:
            _showError(cmd.str);
            break;

        case UiCmdType::HideOverlay:
            _hideOverlay();
            break;

        case UiCmdType::GoToSettings:
            goToSettings();
            break;
        }
    }
}

// =============================================================================
// _processGesture — swipe detection using lv_indev_get_vect()
// =============================================================================
void ScreenManager::_processGesture() {
    lv_indev_t* indev = _touchIndev ? _touchIndev : lv_indev_get_next(nullptr);
    if (!indev) return;

    bool pressed = g_touch_pressed;

    if (pressed) {
        // Use absolute position delta — lv_indev_get_vect() is consumed by LVGL
        // scroll handlers and often returns zero even during a valid swipe.
        lv_point_t pt;
        lv_indev_get_point(indev, &pt);

        if (!_indevWasPressed) {
            // Touch-down: record start position
            _swiping = true;
            _swipeAccX = 0; _swipeAccY = 0;
            _swipeTouchX = pt.x; _swipeTouchY = pt.y;
            _swipeStartMs = millis();
        } else {
            // Accumulate displacement from last known position
            _swipeAccX += pt.x - _swipeTouchX;
            _swipeAccY += pt.y - _swipeTouchY;
            _swipeTouchX = pt.x; _swipeTouchY = pt.y;
        }
    } else if (_indevWasPressed && _swiping) {
        // Touch-up: evaluate gesture
        uint32_t dt = millis() - _swipeStartMs;
        int32_t ax = abs(_swipeAccX), ay = abs(_swipeAccY);

        if (dt <= SWIPE_MAX_MS && (ax >= SWIPE_MIN_DIST_PX || ay >= SWIPE_MIN_DIST_PX)) {
            if (ax > ay * SWIPE_AXIS_RATIO) {
                // Horizontal swipe
                if (_swipeAccX < 0) {
                    // Swipe left (finger moved left) → go to next page (right)
                    if      (_currentPage == PageId::Settings) _navigateTo(PageId::Home);
                    else if (_currentPage == PageId::Home)     _navigateTo(PageId::Calendar);
                    else if (_currentPage == PageId::Calendar) _navigateTo(PageId::Clock);
                } else {
                    // Swipe right (finger moved right) → go to previous page (left)
                    if      (_currentPage == PageId::Clock)    _navigateTo(PageId::Calendar);
                    else if (_currentPage == PageId::Calendar) _navigateTo(PageId::Home);
                    else if (_currentPage == PageId::Home)     _navigateTo(PageId::Settings);
                }
            } else if (ay > ax * SWIPE_AXIS_RATIO) {
                // Vertical swipe
                if (_swipeAccY > 0) {
                    // Swipe down from top → pull-to-refresh
                    // Only trigger from data pages, not settings
                    if (_currentPage != PageId::Settings) {
                        _refreshRequested = true;
                        _showRefreshSpinner();
                    }
                }
            }
        }
        _swiping = false;
    }
    _indevWasPressed = pressed;
}

// =============================================================================
// Overlay
// =============================================================================
void ScreenManager::_createOverlay() {
    // Use lv_layer_top() so overlay is above all screens
    lv_obj_t* top = lv_layer_top();

    _overlayScreen = lv_obj_create(top);
    lv_obj_set_size(_overlayScreen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(_overlayScreen, LV_ALIGN_CENTER, 0, 0);
    lv_hlp_set_bg(_overlayScreen, lv_hlp_hex(COLOR_BG), LV_OPA_90);
    lv_hlp_set_border_none(_overlayScreen);
    lv_hlp_set_radius(_overlayScreen, 0);
    lv_hlp_no_scroll(_overlayScreen);

    _overlaySpinner = lv_spinner_create(_overlayScreen, 1000, 60);
    lv_obj_set_size(_overlaySpinner, 60, 60);
    lv_obj_align(_overlaySpinner, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_arc_color(_overlaySpinner, lv_hlp_hex(COLOR_ACCENT), LV_PART_INDICATOR);

    _overlayMsg = lv_hlp_label(_overlayScreen, "Connecting...", lv_hlp_hex(COLOR_TEXT_PRIMARY), 18);
    lv_label_set_long_mode(_overlayMsg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(_overlayMsg, SCREEN_WIDTH - 60);
    lv_obj_align(_overlayMsg, LV_ALIGN_CENTER, 0, 50);

    _overlayBtn = lv_btn_create(_overlayScreen);
    lv_obj_set_size(_overlayBtn, 160, 44);
    lv_obj_align(_overlayBtn, LV_ALIGN_CENTER, 0, 100);
    lv_hlp_set_bg(_overlayBtn, lv_hlp_hex(COLOR_ACCENT));
    lv_obj_add_event_cb(_overlayBtn, _onOverlayDismiss, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* lblOk = lv_label_create(_overlayBtn);
    lv_label_set_text(lblOk, LV_SYMBOL_OK "  OK");
    lv_hlp_set_font(lblOk, lv_hlp_font(18));
    lv_obj_center(lblOk);
    lv_obj_add_flag(_overlayBtn, LV_OBJ_FLAG_HIDDEN);

    // Refresh spinner (small, top center, semi-transparent)
    _refreshSpinner = lv_spinner_create(top, 800, 60);
    lv_obj_set_size(_refreshSpinner, 36, 36);
    lv_obj_align(_refreshSpinner, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_arc_color(_refreshSpinner, lv_hlp_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_add_flag(_refreshSpinner, LV_OBJ_FLAG_HIDDEN);

    lv_obj_add_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
}

void ScreenManager::_showConnecting(const char* ssid) {
    if (!_overlayScreen) return;
    char buf[96];
    snprintf(buf, sizeof(buf), "Connessione a WiFi...\n%s", ssid ? ssid : "");
    lv_label_set_text(_overlayMsg, buf);
    lv_obj_clear_flag(_overlaySpinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(_overlayBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(_overlayMsg, LV_ALIGN_CENTER, 0, 50);
    lv_obj_clear_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
    _overlayVisible = true;
}

void ScreenManager::_showError(const char* msg) {
    if (!_overlayScreen) return;
    lv_label_set_text(_overlayMsg, msg ? msg : "Error");
    lv_obj_add_flag(_overlaySpinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(_overlayMsg, LV_ALIGN_CENTER, 0, -30);
    lv_obj_clear_flag(_overlayBtn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
    _overlayVisible = true;
}

void ScreenManager::_hideOverlay() {
    if (_overlayScreen && _overlayVisible) {
        lv_obj_add_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
        _overlayVisible = false;
    }
}

void ScreenManager::_showRefreshSpinner() {
    if (_refreshSpinner && !_refreshSpinnerVisible) {
        lv_obj_clear_flag(_refreshSpinner, LV_OBJ_FLAG_HIDDEN);
        _refreshSpinnerVisible = true;
    }
}

void ScreenManager::_hideRefreshSpinner() {
    if (_refreshSpinner && _refreshSpinnerVisible) {
        lv_obj_add_flag(_refreshSpinner, LV_OBJ_FLAG_HIDDEN);
        _refreshSpinnerVisible = false;
    }
}

// =============================================================================
// LVGL timers
// =============================================================================
void ScreenManager::_rtcTimerCb(lv_timer_t* t) {
    ScreenManager* self = (ScreenManager*)t->user_data;
    if (self) self->_engine.updateRtc(self->_tzOffset);
}

void ScreenManager::_minuteTimerCb(lv_timer_t* t) {
    ScreenManager* self = (ScreenManager*)t->user_data;
    if (self) self->_engine.updateRtc(self->_tzOffset);  // date/weekday/month
}

// =============================================================================
// Static callbacks
// =============================================================================
void ScreenManager::_onOverlayDismiss(lv_event_t* e) {
    ScreenManager* self = (ScreenManager*)lv_event_get_user_data(e);
    if (self) self->_hideOverlay();
}

void ScreenManager::_secondTimerCb(lv_timer_t* t) {
    auto* self = static_cast<ScreenManager*>(t->user_data);
    if (self) self->_engine.updateRtc(self->_tzOffset);
}
