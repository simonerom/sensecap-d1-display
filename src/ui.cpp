#include "ui.h"
#include "../include/config.h"

#include <TFT_eSPI.h>
#include <lvgl.h>

// =============================================================================
// TFT + touch driver globals
// =============================================================================
static TFT_eSPI tft = TFT_eSPI();

static const uint32_t LV_BUF_SIZE = SCREEN_WIDTH * 20;
static lv_color_t lv_buf1[LV_BUF_SIZE];
static lv_color_t lv_buf2[LV_BUF_SIZE];
static lv_disp_draw_buf_t lv_draw_buf;
static lv_disp_drv_t lv_disp_drv;
static lv_indev_drv_t lv_touch_drv;

// =============================================================================
// LVGL display flush callback
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
// LVGL touch input callback
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
// LVGL tick ISR
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

    pinMode(TFT_BL, OUTPUT);
    ledcSetup(0, 5000, 8);
    ledcAttachPin(TFT_BL, 0);
    ledcWrite(0, BACKLIGHT_BRIGHTNESS);

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
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 5000));
}

// =============================================================================
// Color helper
// =============================================================================
static lv_color_t hex2color(uint32_t hex) {
    return lv_color_make((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
}

// =============================================================================
// UIManager constructor
// =============================================================================
UIManager::UIManager()
    : _tabview(nullptr), _tabPage1(nullptr), _tabPage2(nullptr), _tabPage3(nullptr),
      _lblTime(nullptr), _lblDateStr(nullptr),
      _lblIndoorTemp(nullptr), _lblIndoorHumidity(nullptr),
      _lblOutdoorTemp(nullptr), _lblOutdoorHumidity(nullptr), _lblSensorStatus(nullptr),
      _lblGreeting(nullptr), _lblMessage(nullptr),
      _lblWeatherIcon(nullptr), _lblWeatherDesc(nullptr),
      _alertBanner(nullptr), _lblAlertText(nullptr),
      _taSSID(nullptr), _taPassword(nullptr), _taHost(nullptr), _taPort(nullptr),
      _spinboxTZ(nullptr), _btnSave(nullptr), _keyboard(nullptr), _activeTA(nullptr),
      _overlayScreen(nullptr), _spinnerOverlay(nullptr), _lblOverlayMsg(nullptr),
      _dotContainer(nullptr), _btnGear(nullptr),
      _currentPage(0), _overlayVisible(false), _settingsCallback(nullptr) {
    for (int i = 0; i < PAGE_COUNT; i++) _dots[i] = nullptr;
}

// =============================================================================
// Dark theme
// =============================================================================
void UIManager::_applyDarkTheme() {
    lv_theme_t* theme = lv_theme_default_init(
        lv_disp_get_default(),
        hex2color(COLOR_ACCENT),
        hex2color(COLOR_ALERT),
        true,
        &lv_font_montserrat_14
    );
    lv_disp_set_theme(lv_disp_get_default(), theme);
    lv_obj_set_style_bg_color(lv_scr_act(), hex2color(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
}

// =============================================================================
// init
// =============================================================================
void UIManager::init(void (*settingsCallback)(const AppSettings&)) {
    _settingsCallback = settingsCallback;
    _applyDarkTheme();

    lv_obj_t* screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, hex2color(COLOR_BG), 0);

    // Tabview: horizontal swipe, hidden tab bar
    _tabview = lv_tabview_create(screen, LV_DIR_LEFT, 0);
    lv_obj_set_size(_tabview, SCREEN_WIDTH, SCREEN_HEIGHT - 20);
    lv_obj_align(_tabview, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_tabview, hex2color(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(_tabview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_tabview, 0, 0);

    _tabPage1 = lv_tabview_add_tab(_tabview, "Clock");
    _tabPage2 = lv_tabview_add_tab(_tabview, "Message");
    _tabPage3 = lv_tabview_add_tab(_tabview, "Settings");

    lv_obj_set_style_bg_color(_tabPage1, hex2color(COLOR_PAGE1), 0);
    lv_obj_set_style_bg_opa(_tabPage1, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(_tabPage2, hex2color(COLOR_PAGE2), 0);
    lv_obj_set_style_bg_opa(_tabPage2, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(_tabPage3, hex2color(COLOR_PAGE3), 0);
    lv_obj_set_style_bg_opa(_tabPage3, LV_OPA_COVER, 0);

    lv_obj_set_style_pad_all(_tabPage1, 12, 0);
    lv_obj_set_style_pad_all(_tabPage2, 12, 0);
    lv_obj_set_style_pad_all(_tabPage3, 12, 0);

    _createPage1(_tabPage1);
    _createPage2(_tabPage2);
    _createPage3(_tabPage3);
    _createNavDots(screen);
    _createGearButton(screen);
    _createOverlay();

    lv_obj_add_event_cb(_tabview, _onTabChanged, LV_EVENT_VALUE_CHANGED, this);
    _updateNavDots(0);
}

// =============================================================================
// Page 1: Clock & Environment
// =============================================================================
void UIManager::_createPage1(lv_obj_t* parent) {
    // Large time display
    _lblTime = lv_label_create(parent);
    lv_label_set_text(_lblTime, "--:--");
    lv_obj_set_style_text_color(_lblTime, hex2color(COLOR_TIME), 0);
    lv_obj_set_style_text_font(_lblTime, &lv_font_montserrat_48, 0);
    lv_obj_align(_lblTime, LV_ALIGN_TOP_MID, 0, 0);

    // Date string below time
    _lblDateStr = lv_label_create(parent);
    lv_label_set_text(_lblDateStr, "---");
    lv_obj_set_style_text_color(_lblDateStr, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(_lblDateStr, &lv_font_montserrat_18, 0);
    lv_obj_align(_lblDateStr, LV_ALIGN_TOP_MID, 0, 58);

    // Divider line
    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_set_size(line, SCREEN_WIDTH - 24, 1);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 86);
    lv_obj_set_style_bg_color(line, hex2color(COLOR_DIVIDER), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ---- Indoor column (left) ----
    lv_obj_t* lblIndoorTitle = lv_label_create(parent);
    lv_label_set_text(lblIndoorTitle, "INDOOR");
    lv_obj_set_style_text_color(lblIndoorTitle, hex2color(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(lblIndoorTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_letter_space(lblIndoorTitle, 2, 0);
    lv_obj_align(lblIndoorTitle, LV_ALIGN_TOP_LEFT, 0, 96);

    _lblIndoorTemp = lv_label_create(parent);
    lv_label_set_text(_lblIndoorTemp, "--C");
    lv_obj_set_style_text_color(_lblIndoorTemp, hex2color(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(_lblIndoorTemp, &lv_font_montserrat_28, 0);
    lv_obj_align(_lblIndoorTemp, LV_ALIGN_TOP_LEFT, 0, 112);

    _lblIndoorHumidity = lv_label_create(parent);
    lv_label_set_text(_lblIndoorHumidity, "--%");
    lv_obj_set_style_text_color(_lblIndoorHumidity, hex2color(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(_lblIndoorHumidity, &lv_font_montserrat_18, 0);
    lv_obj_align(_lblIndoorHumidity, LV_ALIGN_TOP_LEFT, 0, 148);

    _lblSensorStatus = lv_label_create(parent);
    lv_label_set_text(_lblSensorStatus, "");
    lv_obj_set_style_text_color(_lblSensorStatus, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(_lblSensorStatus, &lv_font_montserrat_12, 0);
    lv_obj_align(_lblSensorStatus, LV_ALIGN_TOP_LEFT, 0, 170);

    // ---- Outdoor column (right) ----
    lv_obj_t* lblOutdoorTitle = lv_label_create(parent);
    lv_label_set_text(lblOutdoorTitle, "OUTDOOR");
    lv_obj_set_style_text_color(lblOutdoorTitle, hex2color(COLOR_OUTDOOR), 0);
    lv_obj_set_style_text_font(lblOutdoorTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_letter_space(lblOutdoorTitle, 2, 0);
    lv_obj_align(lblOutdoorTitle, LV_ALIGN_TOP_MID, 60, 96);

    _lblOutdoorTemp = lv_label_create(parent);
    lv_label_set_text(_lblOutdoorTemp, "--C");
    lv_obj_set_style_text_color(_lblOutdoorTemp, hex2color(COLOR_OUTDOOR), 0);
    lv_obj_set_style_text_font(_lblOutdoorTemp, &lv_font_montserrat_28, 0);
    lv_obj_align(_lblOutdoorTemp, LV_ALIGN_TOP_MID, 60, 112);

    _lblOutdoorHumidity = lv_label_create(parent);
    lv_label_set_text(_lblOutdoorHumidity, "--%");
    lv_obj_set_style_text_color(_lblOutdoorHumidity, hex2color(COLOR_OUTDOOR), 0);
    lv_obj_set_style_text_font(_lblOutdoorHumidity, &lv_font_montserrat_18, 0);
    lv_obj_align(_lblOutdoorHumidity, LV_ALIGN_TOP_MID, 60, 148);
}

// =============================================================================
// Page 2: Message + Weather + Alert
// =============================================================================
void UIManager::_createPage2(lv_obj_t* parent) {
    // Greeting
    _lblGreeting = lv_label_create(parent);
    lv_label_set_text(_lblGreeting, "Good Morning");
    lv_obj_set_style_text_color(_lblGreeting, hex2color(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(_lblGreeting, &lv_font_montserrat_22, 0);
    lv_obj_align(_lblGreeting, LV_ALIGN_TOP_LEFT, 0, 0);

    // Message (scrollable area)
    lv_obj_t* msgContainer = lv_obj_create(parent);
    lv_obj_set_size(msgContainer, SCREEN_WIDTH - 24, 120);
    lv_obj_align(msgContainer, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_obj_set_style_bg_color(msgContainer, hex2color(0x0A0A20), 0);
    lv_obj_set_style_bg_opa(msgContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(msgContainer, hex2color(COLOR_DIVIDER), 0);
    lv_obj_set_style_border_width(msgContainer, 1, 0);
    lv_obj_set_style_radius(msgContainer, 6, 0);
    lv_obj_set_style_pad_all(msgContainer, 8, 0);

    _lblMessage = lv_label_create(msgContainer);
    lv_label_set_text(_lblMessage, "Waiting for data...");
    lv_obj_set_style_text_color(_lblMessage, hex2color(COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(_lblMessage, &lv_font_montserrat_18, 0);
    lv_obj_set_width(_lblMessage, SCREEN_WIDTH - 44);
    lv_label_set_long_mode(_lblMessage, LV_LABEL_LONG_WRAP);

    // Weather row
    _lblWeatherIcon = lv_label_create(parent);
    lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_color(_lblWeatherIcon, hex2color(COLOR_WEATHER), 0);
    lv_obj_set_style_text_font(_lblWeatherIcon, &lv_font_montserrat_22, 0);
    lv_obj_align(_lblWeatherIcon, LV_ALIGN_TOP_LEFT, 0, 158);

    _lblWeatherDesc = lv_label_create(parent);
    lv_label_set_text(_lblWeatherDesc, "---");
    lv_obj_set_style_text_color(_lblWeatherDesc, hex2color(COLOR_WEATHER), 0);
    lv_obj_set_style_text_font(_lblWeatherDesc, &lv_font_montserrat_18, 0);
    lv_obj_set_width(_lblWeatherDesc, SCREEN_WIDTH - 60);
    lv_label_set_long_mode(_lblWeatherDesc, LV_LABEL_LONG_DOT);
    lv_obj_align(_lblWeatherDesc, LV_ALIGN_TOP_LEFT, 30, 160);

    // Alert banner (hidden by default)
    _alertBanner = lv_obj_create(parent);
    lv_obj_set_size(_alertBanner, SCREEN_WIDTH - 24, 36);
    lv_obj_align(_alertBanner, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_alertBanner, hex2color(COLOR_ALERT), 0);
    lv_obj_set_style_bg_opa(_alertBanner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_alertBanner, 0, 0);
    lv_obj_set_style_radius(_alertBanner, 4, 0);
    lv_obj_set_style_pad_all(_alertBanner, 4, 0);
    lv_obj_clear_flag(_alertBanner, LV_OBJ_FLAG_SCROLLABLE);

    _lblAlertText = lv_label_create(_alertBanner);
    lv_label_set_text(_lblAlertText, "");
    lv_obj_set_style_text_color(_lblAlertText, hex2color(COLOR_TIME), 0);
    lv_obj_set_style_text_font(_lblAlertText, &lv_font_montserrat_14, 0);
    lv_obj_set_width(_lblAlertText, SCREEN_WIDTH - 40);
    lv_label_set_long_mode(_lblAlertText, LV_LABEL_LONG_DOT);
    lv_obj_align(_lblAlertText, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_add_flag(_alertBanner, LV_OBJ_FLAG_HIDDEN);
}

// =============================================================================
// Page 3: Settings
// =============================================================================
void UIManager::_createPage3(lv_obj_t* parent) {
    lv_obj_t* lblTitle = lv_label_create(parent);
    lv_label_set_text(lblTitle, LV_SYMBOL_SETTINGS "  SETTINGS");
    lv_obj_set_style_text_color(lblTitle, hex2color(COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(lblTitle, 2, 0);
    lv_obj_align(lblTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    // ---- WiFi SSID ----
    lv_obj_t* lblSSID = lv_label_create(parent);
    lv_label_set_text(lblSSID, "WiFi SSID");
    lv_obj_set_style_text_color(lblSSID, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(lblSSID, &lv_font_montserrat_12, 0);
    lv_obj_align(lblSSID, LV_ALIGN_TOP_LEFT, 0, 22);

    _taSSID = lv_textarea_create(parent);
    lv_obj_set_size(_taSSID, 200, 34);
    lv_obj_align(_taSSID, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_textarea_set_one_line(_taSSID, true);
    lv_textarea_set_placeholder_text(_taSSID, "Network name");
    lv_obj_set_style_text_font(_taSSID, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(_taSSID, _onTAFocused, LV_EVENT_FOCUSED, this);

    // ---- WiFi Password ----
    lv_obj_t* lblPass = lv_label_create(parent);
    lv_label_set_text(lblPass, "Password");
    lv_obj_set_style_text_color(lblPass, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(lblPass, &lv_font_montserrat_12, 0);
    lv_obj_align(lblPass, LV_ALIGN_TOP_LEFT, 216, 22);

    _taPassword = lv_textarea_create(parent);
    lv_obj_set_size(_taPassword, 240, 34);
    lv_obj_align(_taPassword, LV_ALIGN_TOP_LEFT, 216, 36);
    lv_textarea_set_one_line(_taPassword, true);
    lv_textarea_set_password_mode(_taPassword, true);
    lv_textarea_set_placeholder_text(_taPassword, "Password");
    lv_obj_set_style_text_font(_taPassword, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(_taPassword, _onTAFocused, LV_EVENT_FOCUSED, this);

    // ---- Server Host ----
    lv_obj_t* lblHost = lv_label_create(parent);
    lv_label_set_text(lblHost, "Server IP");
    lv_obj_set_style_text_color(lblHost, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(lblHost, &lv_font_montserrat_12, 0);
    lv_obj_align(lblHost, LV_ALIGN_TOP_LEFT, 0, 78);

    _taHost = lv_textarea_create(parent);
    lv_obj_set_size(_taHost, 200, 34);
    lv_obj_align(_taHost, LV_ALIGN_TOP_LEFT, 0, 92);
    lv_textarea_set_one_line(_taHost, true);
    lv_textarea_set_placeholder_text(_taHost, "192.168.1.100");
    lv_obj_set_style_text_font(_taHost, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(_taHost, _onTAFocused, LV_EVENT_FOCUSED, this);

    // ---- Server Port ----
    lv_obj_t* lblPort = lv_label_create(parent);
    lv_label_set_text(lblPort, "Port");
    lv_obj_set_style_text_color(lblPort, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(lblPort, &lv_font_montserrat_12, 0);
    lv_obj_align(lblPort, LV_ALIGN_TOP_LEFT, 216, 78);

    _taPort = lv_textarea_create(parent);
    lv_obj_set_size(_taPort, 100, 34);
    lv_obj_align(_taPort, LV_ALIGN_TOP_LEFT, 216, 92);
    lv_textarea_set_one_line(_taPort, true);
    lv_textarea_set_accepted_chars(_taPort, "0123456789");
    lv_textarea_set_placeholder_text(_taPort, "8080");
    lv_obj_set_style_text_font(_taPort, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(_taPort, _onTAFocused, LV_EVENT_FOCUSED, this);

    // ---- Timezone ----
    lv_obj_t* lblTZ = lv_label_create(parent);
    lv_label_set_text(lblTZ, "Timezone (UTC)");
    lv_obj_set_style_text_color(lblTZ, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(lblTZ, &lv_font_montserrat_12, 0);
    lv_obj_align(lblTZ, LV_ALIGN_TOP_LEFT, 332, 78);

    _spinboxTZ = lv_spinbox_create(parent);
    lv_obj_set_size(_spinboxTZ, 100, 34);
    lv_obj_align(_spinboxTZ, LV_ALIGN_TOP_LEFT, 332, 92);
    lv_spinbox_set_range(_spinboxTZ, -12, 14);
    lv_spinbox_set_digit_format(_spinboxTZ, 3, 0);
    lv_spinbox_set_value(_spinboxTZ, TIMEZONE_OFFSET_DEFAULT);
    lv_obj_set_style_text_font(_spinboxTZ, &lv_font_montserrat_14, 0);

    // Spinbox increment/decrement buttons
    lv_obj_t* btnInc = lv_btn_create(parent);
    lv_obj_set_size(btnInc, 28, 28);
    lv_obj_align_to(btnInc, _spinboxTZ, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    lv_obj_set_style_bg_color(btnInc, hex2color(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(btnInc, _onSpinboxIncrement, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* lblInc = lv_label_create(btnInc);
    lv_label_set_text(lblInc, LV_SYMBOL_PLUS);
    lv_obj_center(lblInc);

    lv_obj_t* btnDec = lv_btn_create(parent);
    lv_obj_set_size(btnDec, 28, 28);
    lv_obj_align_to(btnDec, _spinboxTZ, LV_ALIGN_OUT_LEFT_MID, -4, 0);
    lv_obj_set_style_bg_color(btnDec, hex2color(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(btnDec, _onSpinboxDecrement, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* lblDec = lv_label_create(btnDec);
    lv_label_set_text(lblDec, LV_SYMBOL_MINUS);
    lv_obj_center(lblDec);

    // ---- Save button ----
    _btnSave = lv_btn_create(parent);
    lv_obj_set_size(_btnSave, 160, 40);
    lv_obj_align(_btnSave, LV_ALIGN_TOP_RIGHT, 0, 135);
    lv_obj_set_style_bg_color(_btnSave, hex2color(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(_btnSave, _onSaveClicked, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* lblSave = lv_label_create(_btnSave);
    lv_label_set_text(lblSave, LV_SYMBOL_SAVE "  Save & Reboot");
    lv_obj_set_style_text_font(lblSave, &lv_font_montserrat_14, 0);
    lv_obj_center(lblSave);

    // ---- Virtual keyboard (hidden initially) ----
    _keyboard = lv_keyboard_create(lv_scr_act());
    lv_obj_set_size(_keyboard, SCREEN_WIDTH, 160);
    lv_obj_align(_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(_keyboard, _onKeyboardReady, LV_EVENT_READY, this);
    lv_obj_add_event_cb(_keyboard, _onKeyboardReady, LV_EVENT_CANCEL, this);
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
}

// =============================================================================
// Navigation dots
// =============================================================================
void UIManager::_createNavDots(lv_obj_t* parent) {
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

// =============================================================================
// Gear icon button (always visible, top-right)
// =============================================================================
void UIManager::_createGearButton(lv_obj_t* parent) {
    _btnGear = lv_btn_create(parent);
    lv_obj_set_size(_btnGear, 36, 36);
    lv_obj_align(_btnGear, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_color(_btnGear, hex2color(0x333355), 0);
    lv_obj_set_style_bg_opa(_btnGear, LV_OPA_70, 0);
    lv_obj_set_style_border_width(_btnGear, 0, 0);
    lv_obj_set_style_radius(_btnGear, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(_btnGear, _onGearClicked, LV_EVENT_SHORT_CLICKED, this);

    lv_obj_t* ico = lv_label_create(_btnGear);
    lv_label_set_text(ico, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(ico, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_center(ico);
    lv_obj_move_foreground(_btnGear);
}

// =============================================================================
// Overlay (WiFi / error)
// =============================================================================
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
    lv_label_set_text(_lblOverlayMsg, "Connecting...");
    lv_obj_set_style_text_color(_lblOverlayMsg, hex2color(COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(_lblOverlayMsg, &lv_font_montserrat_18, 0);
    lv_obj_align(_lblOverlayMsg, LV_ALIGN_CENTER, 0, 50);

    lv_obj_add_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
}

// =============================================================================
// Public: showConnecting / showError / hideOverlay
// =============================================================================
void UIManager::showConnecting(const String& ssid) {
    if (!_overlayScreen) return;
    String msg = "Connecting to WiFi...\n" + ssid;
    lv_label_set_text(_lblOverlayMsg, msg.c_str());
    lv_obj_clear_flag(_spinnerOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_overlayScreen);
    _overlayVisible = true;
}

void UIManager::showError(const String& msg) {
    if (!_overlayScreen) return;
    lv_label_set_text(_lblOverlayMsg, msg.c_str());
    lv_obj_add_flag(_spinnerOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_overlayScreen);
    _overlayVisible = true;
}

void UIManager::hideOverlay() {
    if (_overlayScreen && _overlayVisible) {
        lv_obj_add_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
        _overlayVisible = false;
    }
}

void UIManager::goToSettings() {
    if (_tabview) {
        lv_tabview_set_act(_tabview, 2, LV_ANIM_ON);
        _updateNavDots(2);
    }
}

// =============================================================================
// populateSettings — pre-fill settings textareas
// =============================================================================
void UIManager::populateSettings(const AppSettings& s) {
    if (_taSSID)    lv_textarea_set_text(_taSSID,    s.wifiSSID.c_str());
    if (_taPassword) lv_textarea_set_text(_taPassword, s.wifiPassword.c_str());
    if (_taHost)    lv_textarea_set_text(_taHost,    s.serverHost.c_str());
    if (_taPort)    lv_textarea_set_text(_taPort,    String(s.serverPort).c_str());
    if (_spinboxTZ) lv_spinbox_set_value(_spinboxTZ, s.timezoneOffset);
}

// =============================================================================
// updateData — update UI with server data
// =============================================================================
void UIManager::updateData(const DisplayData& data) {
    hideOverlay();

    if (!data.valid) return;

    // Page 1: time + date + outdoor
    if (_lblTime)    lv_label_set_text(_lblTime, data.time.isEmpty() ? "--:--" : data.time.c_str());
    if (_lblDateStr) lv_label_set_text(_lblDateStr, data.date.c_str());

    if (_lblOutdoorTemp) {
        String t = data.tempOutdoor.isEmpty() ? "--" : data.tempOutdoor;
        // Ensure there's a unit suffix
        if (t.indexOf('C') < 0 && t.indexOf('F') < 0 && t != "--")
            t += "C";
        lv_label_set_text(_lblOutdoorTemp, t.c_str());
    }
    if (_lblOutdoorHumidity) {
        String h = data.humidityOutdoor.isEmpty() ? "--" : data.humidityOutdoor;
        if (h != "--" && h.indexOf('%') < 0) h += "%";
        lv_label_set_text(_lblOutdoorHumidity, h.c_str());
    }

    // Page 2: greeting + message + weather + alert
    if (_lblGreeting) {
        // Build greeting from time field or just use "message" prefix
        String greeting = "Good Morning";
        if (!data.time.isEmpty()) {
            int hour = data.time.substring(0, 2).toInt();
            if (hour >= 5 && hour < 12) greeting = "Good Morning";
            else if (hour >= 12 && hour < 18) greeting = "Good Afternoon";
            else greeting = "Good Evening";
        }
        lv_label_set_text(_lblGreeting, greeting.c_str());
    }

    if (_lblMessage) lv_label_set_text(_lblMessage, data.message.isEmpty() ? "---" : data.message.c_str());

    if (_lblWeatherDesc) lv_label_set_text(_lblWeatherDesc, data.weather.isEmpty() ? "---" : data.weather.c_str());

    if (_lblWeatherIcon) {
        String w = data.weather;
        w.toLowerCase();
        if (w.indexOf("sun") >= 0 || w.indexOf("clear") >= 0)
            lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_POWER);
        else if (w.indexOf("cloud") >= 0 || w.indexOf("overcast") >= 0)
            lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_UPLOAD);
        else if (w.indexOf("rain") >= 0 || w.indexOf("drizzle") >= 0)
            lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_DOWNLOAD);
        else if (w.indexOf("snow") >= 0)
            lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_IMAGE);
        else
            lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_EYE_OPEN);
    }

    // Alert banner
    if (_alertBanner && _lblAlertText) {
        bool hasAlert = !data.alert.isEmpty() && data.alert != "-" && data.alert != "none";
        if (hasAlert) {
            lv_label_set_text(_lblAlertText, data.alert.c_str());
            lv_obj_clear_flag(_alertBanner, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_alertBanner, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// =============================================================================
// updateSensor — update indoor sensor values
// =============================================================================
void UIManager::updateSensor(float tempC, float humidityPct, bool available) {
    if (!available) {
        if (_lblIndoorTemp)     lv_label_set_text(_lblIndoorTemp, "--C");
        if (_lblIndoorHumidity) lv_label_set_text(_lblIndoorHumidity, "--%");
        if (_lblSensorStatus)   lv_label_set_text(_lblSensorStatus, "No sensor");
        return;
    }

    if (_lblSensorStatus) lv_label_set_text(_lblSensorStatus, "");

    char buf[16];
    if (_lblIndoorTemp) {
        snprintf(buf, sizeof(buf), "%.1fC", tempC);
        lv_label_set_text(_lblIndoorTemp, buf);
    }
    if (_lblIndoorHumidity) {
        snprintf(buf, sizeof(buf), "%.0f%%", humidityPct);
        lv_label_set_text(_lblIndoorHumidity, buf);
    }
}

// =============================================================================
// tick
// =============================================================================
void UIManager::tick() {
    lv_timer_handler();
}

// =============================================================================
// Keyboard helpers
// =============================================================================
void UIManager::_showKeyboard(lv_obj_t* ta) {
    _activeTA = ta;
    lv_keyboard_set_textarea(_keyboard, ta);
    lv_obj_clear_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_keyboard);
}

void UIManager::_hideKeyboard() {
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    _activeTA = nullptr;
}

// =============================================================================
// Static event callbacks
// =============================================================================
void UIManager::_onTabChanged(lv_event_t* e) {
    UIManager* self = (UIManager*)lv_event_get_user_data(e);
    if (!self || !self->_tabview) return;
    uint16_t page = lv_tabview_get_tab_act(self->_tabview);
    self->_updateNavDots(page);
    // Hide keyboard when leaving settings page
    if (page != 2 && self->_keyboard) {
        self->_hideKeyboard();
    }
}

void UIManager::_onTAFocused(lv_event_t* e) {
    UIManager* self = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* ta = lv_event_get_target(e);
    if (self && ta) {
        self->_showKeyboard(ta);
    }
}

void UIManager::_onKeyboardReady(lv_event_t* e) {
    UIManager* self = (UIManager*)lv_event_get_user_data(e);
    if (self) self->_hideKeyboard();
}

void UIManager::_onSaveClicked(lv_event_t* e) {
    UIManager* self = (UIManager*)lv_event_get_user_data(e);
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

    if (self->_settingsCallback) {
        self->_settingsCallback(s);
    }
}

void UIManager::_onGearClicked(lv_event_t* e) {
    UIManager* self = (UIManager*)lv_event_get_user_data(e);
    if (self) self->goToSettings();
}

void UIManager::_onSpinboxIncrement(lv_event_t* e) {
    UIManager* self = (UIManager*)lv_event_get_user_data(e);
    if (self && self->_spinboxTZ) lv_spinbox_increment(self->_spinboxTZ);
}

void UIManager::_onSpinboxDecrement(lv_event_t* e) {
    UIManager* self = (UIManager*)lv_event_get_user_data(e);
    if (self && self->_spinboxTZ) lv_spinbox_decrement(self->_spinboxTZ);
}
