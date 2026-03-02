#pragma once

#include <lvgl.h>
#include "data_fetcher.h"
#include "settings_manager.h"
#include "../include/config.h"

// Number of calibration points
#define CAL_POINT_COUNT 5

// =============================================================================
// UIManager - 3-page LVGL UI with horizontal swipe
//
// Page 0 (Clock & Env): Large time, date, indoor/outdoor temp+humidity
// Page 1 (Message):     Greeting, daily message, weather, alert banner
// Page 2 (Settings):    WiFi, server, timezone, Save button
// =============================================================================

class UIManager {
public:
    UIManager();

    // Initialize LVGL and build all pages.
    // settingsCallback is called when user taps Save on Page 3.
    // calDoneCallback is called when touch calibration completes.
    void init(void (*settingsCallback)(const AppSettings&) = nullptr,
              void (*calDoneCallback)(const TouchCalibration&) = nullptr);

    // Update display with new server/sensor data (call from network task under mutex)
    void updateData(const DisplayData& data);

    // Update only indoor sensor values (call from sensor task under mutex)
    void updateSensor(float tempC, float humidityPct, bool available);

    // Show WiFi connecting overlay
    void showConnecting(const String& ssid);

    // Show error overlay
    void showError(const String& msg);

    // Hide overlay (after WiFi connected)
    void hideOverlay();

    // Navigate to settings page (page index 2)
    void goToSettings();

    // Open the fullscreen touch calibration screen
    void startCalibration();

    // Apply calibration from NVS (called at boot from main.cpp)
    void applyCalibration(const TouchCalibration& cal);

    // LVGL timer handler — call from UI task every ~5ms
    void tick();

    // Pre-fill settings fields with current values
    void populateSettings(const AppSettings& settings);

    int currentPage() const { return _currentPage; }

private:
    // ---- Tabview ----
    lv_obj_t* _tabview;
    lv_obj_t* _tabPage1;   // Clock & Env
    lv_obj_t* _tabPage2;   // Message
    lv_obj_t* _tabPage3;   // Settings

    // ---- Page 1: Clock & Environment ----
    lv_obj_t* _lblTime;           // HH:MM (48pt)
    lv_obj_t* _lblDateStr;        // "Monday, March 2"
    lv_obj_t* _lblIndoorTemp;     // indoor temperature
    lv_obj_t* _lblIndoorHumidity; // indoor humidity
    lv_obj_t* _lblOutdoorTemp;    // outdoor temperature
    lv_obj_t* _lblOutdoorHumidity;// outdoor humidity
    lv_obj_t* _lblSensorStatus;   // "No sensor" if unavailable

    // ---- Page 2: Message ----
    lv_obj_t* _lblGreeting;       // "Good Morning/Afternoon/Evening"
    lv_obj_t* _lblMessage;        // scrollable message
    lv_obj_t* _lblWeatherIcon;    // weather emoji/symbol
    lv_obj_t* _lblWeatherDesc;    // weather description text
    lv_obj_t* _alertBanner;       // red banner container
    lv_obj_t* _lblAlertText;      // alert text inside banner

    // ---- Page 3: Settings ----
    lv_obj_t* _taSSID;            // textarea: WiFi SSID
    lv_obj_t* _taPassword;        // textarea: WiFi Password
    lv_obj_t* _taHost;            // textarea: Server host
    lv_obj_t* _taPort;            // textarea: Server port
    lv_obj_t* _spinboxTZ;         // spinbox: timezone offset
    lv_obj_t* _btnSave;           // Save & Reboot button
    lv_obj_t* _btnShowPwd;        // toggle password visibility
    lv_obj_t* _keyboard;          // virtual keyboard (inside kbd panel)
    lv_obj_t* _kbdPanel;          // fullscreen keyboard panel
    lv_obj_t* _kbdFieldLabel;     // field name label inside panel
    lv_obj_t* _kbdPreview;        // large textarea preview inside panel
    lv_obj_t* _activeTA;          // currently focused textarea (settings page)

    // ---- Overlay (WiFi connecting / error) ----
    lv_obj_t* _overlayScreen;
    lv_obj_t* _spinnerOverlay;
    lv_obj_t* _lblOverlayMsg;
    lv_obj_t* _btnOverlayDismiss;  // "OK" button shown on error overlays

    // ---- Nav dots ----
    lv_obj_t* _dotContainer;
    lv_obj_t* _dots[PAGE_COUNT];

    // ---- Gear icon button (always on top) ----
    lv_obj_t* _btnGear;

    // ---- Calibration screen ----
    lv_obj_t* _calScreen;       // fullscreen calibration overlay
    lv_obj_t* _calCrosshair;    // crosshair label (+)
    lv_obj_t* _calInstruction;  // "Tap the crosshair" label
    lv_obj_t* _calProgress;     // "Step 1/5" label
    lv_obj_t* _calBtnSkip;      // Skip button
    int       _calStep;         // current step 0..CAL_POINT_COUNT
    int16_t   _calRawX[CAL_POINT_COUNT];
    int16_t   _calRawY[CAL_POINT_COUNT];

    int   _currentPage;
    bool  _overlayVisible;
    void (*_settingsCallback)(const AppSettings&);
    void (*_calDoneCallback)(const TouchCalibration&);

    void _createPage1(lv_obj_t* parent);
    void _createPage2(lv_obj_t* parent);
    void _createPage3(lv_obj_t* parent);
    void _createNavDots(lv_obj_t* parent);
    void _updateNavDots(int activePage);
    void _createOverlay();
    void _createGearButton(lv_obj_t* parent);
    void _createCalibrationScreen();
    void _applyDarkTheme();
    void _showKeyboard(lv_obj_t* ta);
    void _hideKeyboard();
    void _advanceCalStep();
    void _finishCalibration();

    static void _onTabChanged(lv_event_t* e);
    static void _onTAFocused(lv_event_t* e);
    static void _onKeyboardReady(lv_event_t* e);
    static void _onSaveClicked(lv_event_t* e);
    static void _onGearClicked(lv_event_t* e);
    static void _onSpinboxIncrement(lv_event_t* e);
    static void _onSpinboxDecrement(lv_event_t* e);
    static void _onShowPwdClicked(lv_event_t* e);
    static void _onOverlayDismiss(lv_event_t* e);
    static void _onCalTap(lv_event_t* e);
    static void _onCalSkip(lv_event_t* e);
    static void _onCalibrateClicked(lv_event_t* e);
};

// LVGL display driver setup (Arduino_GFX + PCA9535 RGB panel)
void lvgl_display_init();
void lvgl_touch_init();
void lvgl_tick_timer_init();
