#include "ui.h"
#include "../include/config.h"

#include <TFT_eSPI.h>
#include <lvgl.h>

// =============================================================================
// TFT and touch drivers (globals for the LVGL driver)
// =============================================================================
static TFT_eSPI tft = TFT_eSPI();

// LVGL framebuffer (double buffer for smooth rendering)
static const uint32_t LV_BUF_SIZE = SCREEN_WIDTH * 20;
static lv_color_t lv_buf1[LV_BUF_SIZE];
static lv_color_t lv_buf2[LV_BUF_SIZE];
static lv_disp_draw_buf_t lv_draw_buf;
static lv_disp_drv_t lv_disp_drv;
static lv_indev_drv_t lv_touch_drv;

// =============================================================================
// LVGL callback for display flush
// =============================================================================
static void disp_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_map) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)color_map, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

// =============================================================================
// LVGL callback for touch input
// =============================================================================
static void touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    uint16_t tx, ty;
    bool pressed = tft.getTouch(&tx, &ty);
    if (pressed) {
        data->point.x = tx;
        data->point.y = ty;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// =============================================================================
// Tick timer (called every 5ms via FreeRTOS ticker)
// =============================================================================
static void IRAM_ATTR lvgl_tick_isr(void* arg) {
    lv_tick_inc(5);
}

// =============================================================================
// Display initialization
// =============================================================================
void lvgl_display_init() {
    tft.begin();
    tft.setRotation(DISPLAY_ROTATION);
    tft.fillScreen(TFT_BLACK);

    // Backlight
    pinMode(TFT_BL, OUTPUT);
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_BL, 0);
    ledcWrite(0, BACKLIGHT_BRIGHTNESS);

    // Touch calibration (indicative values for SenseCAP D1)
    uint16_t calData[5] = {275, 3620, 264, 3532, 1};
    tft.setTouch(calData);

    lv_init();

    lv_disp_draw_buf_init(&lv_draw_buf, lv_buf1, lv_buf2, LV_BUF_SIZE);

    lv_disp_drv_init(&lv_disp_drv);
    lv_disp_drv.hor_res  = SCREEN_WIDTH;
    lv_disp_drv.ver_res  = SCREEN_HEIGHT;
    lv_disp_drv.flush_cb = disp_flush_cb;
    lv_disp_drv.draw_buf = &lv_draw_buf;
    lv_disp_drv_register(&lv_disp_drv);

    lv_indev_drv_init(&lv_touch_drv);
    lv_touch_drv.type    = LV_INDEV_TYPE_POINTER;
    lv_touch_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&lv_touch_drv);
}

void lvgl_tick_timer_init() {
    const esp_timer_create_args_t timer_args = {
        .callback = &lvgl_tick_isr,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 5000)); // 5ms
}

// =============================================================================
// Color helper: converts hex RGB to lv_color_t
// =============================================================================
static lv_color_t hex2color(uint32_t hex) {
    return lv_color_make((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
}

// =============================================================================
// UIManager
// =============================================================================
UIManager::UIManager()
    : _tabview(nullptr), _tabPage1(nullptr), _tabPage2(nullptr), _tabPage3(nullptr),
      _overlayScreen(nullptr), _lblDate(nullptr), _lblMessage(nullptr),
      _lblP1Status(nullptr), _lblWeatherIcon(nullptr), _lblWeatherText(nullptr),
      _lblP2Status(nullptr), _lblAlertIcon(nullptr), _lblAlertText(nullptr),
      _lblP3Status(nullptr), _dotContainer(nullptr), _lblOverlayMsg(nullptr),
      _spinnerOverlay(nullptr), _currentPage(0), _overlayVisible(false) {
    for (int i = 0; i < 3; i++) _dots[i] = nullptr;
}

void UIManager::_applyDarkTheme() {
    lv_theme_t* theme = lv_theme_default_init(
        lv_disp_get_default(),
        hex2color(COLOR_ACCENT),
        hex2color(COLOR_ALERT),
        true,   // dark mode
        &lv_font_montserrat_14
    );
    lv_disp_set_theme(lv_disp_get_default(), theme);

    lv_obj_set_style_bg_color(lv_scr_act(), hex2color(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
}

void UIManager::init() {
    _applyDarkTheme();

    lv_obj_t* screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, hex2color(COLOR_BG), 0);

    // Tabview with horizontal swipe, hidden tab bar (we use custom dots)
    _tabview = lv_tabview_create(screen, LV_DIR_LEFT, 0);
    lv_obj_set_size(_tabview, SCREEN_WIDTH, SCREEN_HEIGHT - 24);
    lv_obj_align(_tabview, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_tabview, hex2color(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(_tabview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_tabview, 0, 0);

    // Create tabs (pages)
    _tabPage1 = lv_tabview_add_tab(_tabview, "Info");
    _tabPage2 = lv_tabview_add_tab(_tabview, "Weather");
    _tabPage3 = lv_tabview_add_tab(_tabview, "Alert");

    // Page backgrounds
    lv_obj_set_style_bg_color(_tabPage1, hex2color(COLOR_PAGE1), 0);
    lv_obj_set_style_bg_opa(_tabPage1, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(_tabPage2, hex2color(COLOR_PAGE2), 0);
    lv_obj_set_style_bg_opa(_tabPage2, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(_tabPage3, hex2color(COLOR_PAGE3), 0);
    lv_obj_set_style_bg_opa(_tabPage3, LV_OPA_COVER, 0);

    // Page padding
    lv_obj_set_style_pad_all(_tabPage1, 16, 0);
    lv_obj_set_style_pad_all(_tabPage2, 16, 0);
    lv_obj_set_style_pad_all(_tabPage3, 16, 0);

    _createPage1(_tabPage1);
    _createPage2(_tabPage2);
    _createPage3(_tabPage3);
    _createNavDots(screen);
    _createOverlay();

    // Tab change listener
    lv_obj_add_event_cb(_tabview, _onTabChanged, LV_EVENT_VALUE_CHANGED, this);

    _updateNavDots(0);
}

void UIManager::_createPage1(lv_obj_t* parent) {
    // Page title
    lv_obj_t* lblTitle = lv_label_create(parent);
    lv_label_set_text(lblTitle, "INFORMATION");
    lv_obj_set_style_text_color(lblTitle, hex2color(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(lblTitle, 3, 0);
    lv_obj_align(lblTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    // Separator
    lv_obj_t* line = lv_line_create(parent);
    static lv_point_t pts[2] = {{0, 0}, {SCREEN_WIDTH - 32, 0}};
    lv_line_set_points(line, pts, 2);
    lv_obj_set_style_line_color(line, hex2color(COLOR_ACCENT), 0);
    lv_obj_set_style_line_width(line, 1, 0);
    lv_obj_set_style_line_opa(line, LV_OPA_50, 0);
    lv_obj_align(line, LV_ALIGN_TOP_LEFT, 0, 22);

    // Date/time - large font
    _lblDate = lv_label_create(parent);
    lv_label_set_text(_lblDate, "--");
    lv_obj_set_style_text_color(_lblDate, hex2color(COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(_lblDate, &lv_font_montserrat_28, 0);
    lv_obj_set_width(_lblDate, SCREEN_WIDTH - 32);
    lv_label_set_long_mode(_lblDate, LV_LABEL_LONG_WRAP);
    lv_obj_align(_lblDate, LV_ALIGN_TOP_LEFT, 0, 34);

    // Message
    _lblMessage = lv_label_create(parent);
    lv_label_set_text(_lblMessage, "Waiting for data...");
    lv_obj_set_style_text_color(_lblMessage, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(_lblMessage, &lv_font_montserrat_18, 0);
    lv_obj_set_width(_lblMessage, SCREEN_WIDTH - 32);
    lv_label_set_long_mode(_lblMessage, LV_LABEL_LONG_WRAP);
    lv_obj_align(_lblMessage, LV_ALIGN_TOP_LEFT, 0, 90);

    // Update status
    _lblP1Status = lv_label_create(parent);
    lv_label_set_text(_lblP1Status, "");
    lv_obj_set_style_text_color(_lblP1Status, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(_lblP1Status, &lv_font_montserrat_12, 0);
    lv_obj_align(_lblP1Status, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

void UIManager::_createPage2(lv_obj_t* parent) {
    // Page title
    lv_obj_t* lblTitle = lv_label_create(parent);
    lv_label_set_text(lblTitle, "WEATHER");
    lv_obj_set_style_text_color(lblTitle, hex2color(COLOR_WEATHER), 0);
    lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(lblTitle, 3, 0);
    lv_obj_align(lblTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    // Separator
    lv_obj_t* line = lv_line_create(parent);
    static lv_point_t pts[2] = {{0, 0}, {SCREEN_WIDTH - 32, 0}};
    lv_line_set_points(line, pts, 2);
    lv_obj_set_style_line_color(line, hex2color(COLOR_WEATHER), 0);
    lv_obj_set_style_line_width(line, 1, 0);
    lv_obj_set_style_line_opa(line, LV_OPA_50, 0);
    lv_obj_align(line, LV_ALIGN_TOP_LEFT, 0, 22);

    // Weather icon (large unicode symbol)
    _lblWeatherIcon = lv_label_create(parent);
    lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_color(_lblWeatherIcon, hex2color(COLOR_WEATHER), 0);
    lv_obj_set_style_text_font(_lblWeatherIcon, &lv_font_montserrat_48, 0);
    lv_obj_align(_lblWeatherIcon, LV_ALIGN_TOP_LEFT, 0, 36);

    // Weather text
    _lblWeatherText = lv_label_create(parent);
    lv_label_set_text(_lblWeatherText, "Waiting for data...");
    lv_obj_set_style_text_color(_lblWeatherText, hex2color(COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(_lblWeatherText, &lv_font_montserrat_22, 0);
    lv_obj_set_width(_lblWeatherText, SCREEN_WIDTH - 32);
    lv_label_set_long_mode(_lblWeatherText, LV_LABEL_LONG_WRAP);
    lv_obj_align(_lblWeatherText, LV_ALIGN_TOP_LEFT, 0, 100);

    // Status
    _lblP2Status = lv_label_create(parent);
    lv_label_set_text(_lblP2Status, "");
    lv_obj_set_style_text_color(_lblP2Status, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(_lblP2Status, &lv_font_montserrat_12, 0);
    lv_obj_align(_lblP2Status, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

void UIManager::_createPage3(lv_obj_t* parent) {
    // Page title
    lv_obj_t* lblTitle = lv_label_create(parent);
    lv_label_set_text(lblTitle, "ALERT");
    lv_obj_set_style_text_color(lblTitle, hex2color(COLOR_ALERT), 0);
    lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(lblTitle, 3, 0);
    lv_obj_align(lblTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    // Separator
    lv_obj_t* line = lv_line_create(parent);
    static lv_point_t pts[2] = {{0, 0}, {SCREEN_WIDTH - 32, 0}};
    lv_line_set_points(line, pts, 2);
    lv_obj_set_style_line_color(line, hex2color(COLOR_ALERT), 0);
    lv_obj_set_style_line_width(line, 1, 0);
    lv_obj_set_style_line_opa(line, LV_OPA_50, 0);
    lv_obj_align(line, LV_ALIGN_TOP_LEFT, 0, 22);

    // Alert icon
    _lblAlertIcon = lv_label_create(parent);
    lv_label_set_text(_lblAlertIcon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(_lblAlertIcon, hex2color(COLOR_ALERT), 0);
    lv_obj_set_style_text_font(_lblAlertIcon, &lv_font_montserrat_48, 0);
    lv_obj_align(_lblAlertIcon, LV_ALIGN_TOP_LEFT, 0, 36);

    // Alert text
    _lblAlertText = lv_label_create(parent);
    lv_label_set_text(_lblAlertText, "No active alerts");
    lv_obj_set_style_text_color(_lblAlertText, hex2color(COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(_lblAlertText, &lv_font_montserrat_22, 0);
    lv_obj_set_width(_lblAlertText, SCREEN_WIDTH - 32);
    lv_label_set_long_mode(_lblAlertText, LV_LABEL_LONG_WRAP);
    lv_obj_align(_lblAlertText, LV_ALIGN_TOP_LEFT, 0, 100);

    // Status
    _lblP3Status = lv_label_create(parent);
    lv_label_set_text(_lblP3Status, "");
    lv_obj_set_style_text_color(_lblP3Status, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(_lblP3Status, &lv_font_montserrat_12, 0);
    lv_obj_align(_lblP3Status, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

void UIManager::_createNavDots(lv_obj_t* parent) {
    // Navigation dots container at the bottom
    _dotContainer = lv_obj_create(parent);
    lv_obj_set_size(_dotContainer, PAGE_COUNT * (NAV_DOT_SIZE + NAV_DOT_GAP), NAV_DOT_SIZE + 8);
    lv_obj_align(_dotContainer, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_opa(_dotContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_dotContainer, 0, 0);
    lv_obj_set_style_pad_all(_dotContainer, 0, 0);
    lv_obj_clear_flag(_dotContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(_dotContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(_dotContainer, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(_dotContainer, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(_dotContainer, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_column(_dotContainer, NAV_DOT_GAP - NAV_DOT_SIZE, 0);

    for (int i = 0; i < PAGE_COUNT; i++) {
        _dots[i] = lv_obj_create(_dotContainer);
        lv_obj_set_size(_dots[i], NAV_DOT_SIZE, NAV_DOT_SIZE);
        lv_obj_set_style_radius(_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(_dots[i], 0, 0);
        lv_obj_set_style_bg_color(_dots[i], hex2color(COLOR_DOT_INACTIVE), 0);
        lv_obj_clear_flag(_dots[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
}

void UIManager::_updateNavDots(int activePage) {
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (_dots[i]) {
            if (i == activePage) {
                lv_obj_set_style_bg_color(_dots[i], hex2color(COLOR_DOT_ACTIVE), 0);
                lv_obj_set_size(_dots[i], NAV_DOT_SIZE + 4, NAV_DOT_SIZE);
                lv_obj_set_style_radius(_dots[i], 4, 0);
            } else {
                lv_obj_set_style_bg_color(_dots[i], hex2color(COLOR_DOT_INACTIVE), 0);
                lv_obj_set_size(_dots[i], NAV_DOT_SIZE, NAV_DOT_SIZE);
                lv_obj_set_style_radius(_dots[i], LV_RADIUS_CIRCLE, 0);
            }
        }
    }
    _currentPage = activePage;
}

void UIManager::_createOverlay() {
    _overlayScreen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_overlayScreen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(_overlayScreen, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_overlayScreen, hex2color(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(_overlayScreen, LV_OPA_90, 0);
    lv_obj_set_style_border_width(_overlayScreen, 0, 0);
    lv_obj_set_style_radius(_overlayScreen, 0, 0);
    lv_obj_clear_flag(_overlayScreen, LV_OBJ_FLAG_SCROLLABLE);

    _spinnerOverlay = lv_spinner_create(_overlayScreen, 1000, 60);
    lv_obj_set_size(_spinnerOverlay, 60, 60);
    lv_obj_align(_spinnerOverlay, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_arc_color(_spinnerOverlay, hex2color(COLOR_ACCENT), LV_PART_INDICATOR);

    _lblOverlayMsg = lv_label_create(_overlayScreen);
    lv_label_set_text(_lblOverlayMsg, "Connecting to WiFi...");
    lv_obj_set_style_text_color(_lblOverlayMsg, hex2color(COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(_lblOverlayMsg, &lv_font_montserrat_18, 0);
    lv_obj_align(_lblOverlayMsg, LV_ALIGN_CENTER, 0, 50);

    lv_obj_add_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
}

void UIManager::showConnecting() {
    if (_overlayScreen) {
        lv_label_set_text(_lblOverlayMsg, "Connecting to WiFi...");
        lv_obj_set_style_bg_opa(_overlayScreen, LV_OPA_90, 0);
        lv_obj_clear_flag(_spinnerOverlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_overlayScreen);
        _overlayVisible = true;
    }
}

void UIManager::showError(const String& msg) {
    if (_overlayScreen) {
        lv_label_set_text(_lblOverlayMsg, msg.c_str());
        lv_obj_add_flag(_spinnerOverlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_overlayScreen);
        _overlayVisible = true;
    }
}

void UIManager::updateData(const DisplayData& data) {
    // Hide overlay
    if (_overlayScreen && _overlayVisible) {
        lv_obj_add_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
        _overlayVisible = false;
    }

    if (!data.valid) {
        // Show error but do not block the UI
        String errMsg = "Data not available";
        if (_lblP1Status) lv_label_set_text(_lblP1Status, errMsg.c_str());
        if (_lblP2Status) lv_label_set_text(_lblP2Status, errMsg.c_str());
        if (_lblP3Status) lv_label_set_text(_lblP3Status, errMsg.c_str());
        return;
    }

    // Page 1: Date + Message
    if (_lblDate)    lv_label_set_text(_lblDate,    data.date.c_str());
    if (_lblMessage) lv_label_set_text(_lblMessage, data.message.c_str());

    // Update timestamp
    uint32_t secsAgo = (millis() - data.fetchedAt) / 1000;
    String ts = "Updated " + String(secsAgo) + "s ago";
    if (_lblP1Status) lv_label_set_text(_lblP1Status, ts.c_str());
    if (_lblP2Status) lv_label_set_text(_lblP2Status, ts.c_str());
    if (_lblP3Status) lv_label_set_text(_lblP3Status, ts.c_str());

    // Page 2: Weather
    if (_lblWeatherText) lv_label_set_text(_lblWeatherText, data.weather.c_str());

    // Weather icon based on keywords
    if (_lblWeatherIcon) {
        String w = data.weather;
        w.toLowerCase();
        if (w.indexOf("sun") >= 0 || w.indexOf("sunny") >= 0 || w.indexOf("clear") >= 0)
            lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_POWER);
        else if (w.indexOf("cloud") >= 0 || w.indexOf("overcast") >= 0)
            lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_UPLOAD);
        else if (w.indexOf("rain") >= 0 || w.indexOf("drizzle") >= 0)
            lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_DOWNLOAD);
        else
            lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_EYE_OPEN);
    }

    // Page 3: Alert
    if (_lblAlertText) {
        if (data.alert.isEmpty() || data.alert == "none" || data.alert == "-") {
            lv_label_set_text(_lblAlertText, "No active alerts");
            if (_lblAlertIcon)
                lv_obj_set_style_text_color(_lblAlertIcon, hex2color(COLOR_ACCENT), 0);
            if (_lblAlertText)
                lv_obj_set_style_text_color(_lblAlertText, hex2color(COLOR_TEXT_SECONDARY), 0);
        } else {
            lv_label_set_text(_lblAlertText, data.alert.c_str());
            if (_lblAlertIcon)
                lv_obj_set_style_text_color(_lblAlertIcon, hex2color(COLOR_ALERT), 0);
            if (_lblAlertText)
                lv_obj_set_style_text_color(_lblAlertText, hex2color(COLOR_ALERT), 0);
        }
    }
}

void UIManager::tick() {
    lv_timer_handler();
}

void UIManager::_onTabChanged(lv_event_t* e) {
    UIManager* self = (UIManager*)lv_event_get_user_data(e);
    if (!self || !self->_tabview) return;
    uint16_t page = lv_tabview_get_tab_act(self->_tabview);
    self->_updateNavDots(page);
}
