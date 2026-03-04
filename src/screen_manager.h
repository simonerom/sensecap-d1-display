// =============================================================================
// screen_manager.h — Page navigation, gesture detection, UI queue
//
// Owns the four page screens (Settings, Home, Calendar, Clock).
// Handles:
//   - XML layout build (via XmlParser + WidgetFactory)
//   - Swipe gesture detection using lv_indev_get_vect()
//   - Pull-to-refresh (swipe down)
//   - WiFi/error overlay
//   - FreeRTOS UI command queue (all LVGL calls on Core 1)
// =============================================================================
#pragma once

#include <lvgl.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <functional>
#include <map>
#include <vector>

#include "placeholder_engine.h"
#include "xml_parser.h"
#include "widget_factory.h"
#include "settings_page.h"
#include "settings_manager.h"
#include "data_fetcher.h"
#include "lv_helpers.h"
#include "../include/config.h"

// Navigation order:
//   index 0 = Settings  (left of Home, only reachable from Home)
//   index 1 = Home
//   index 2 = Calendar  (right of Home)
//   index 3 = Clock     (right of Calendar)
enum class PageId : uint8_t { Settings = 0, Home = 1, Calendar = 2, Clock = 3 };

// UI command types posted from non-UI tasks
enum class UiCmdType : uint8_t {
    DataUpdate,      // new DataPayload received
    SensorUpdate,    // new sensor reading
    RebuildLayout,   // new layout XML received (pointer in cmd)
    ShowConnecting,  // show WiFi connecting overlay
    ShowError,       // show error overlay
    HideOverlay,     // hide overlay
    GoToSettings,    // navigate to settings page
};

struct UiCommand {
    UiCmdType type;
    float     f0 = 0, f1 = 0;    // sensor: temp, hum
    float     f2 = 0, f3 = 0;    // sensor: tvoc (raw), co2 (ppm)
    bool      b0 = false;         // sensor: ok flag / overlay: show dismiss btn
    char      str[80] = {};       // error msg / ssid
    DataPayload* dataPtr = nullptr; // heap-alloc DataPayload (type==DataUpdate) — freed after use
    char*     xmlPtr  = nullptr;  // ps_malloc'd XML buffer (type==RebuildLayout) — freed after use
    size_t    xmlLen  = 0;
};

class ScreenManager {
public:
    ScreenManager();

    // ---- Initialization ----
    // Call once from setup(), after lvgl_display_init() + lvgl_touch_init().
    // Callbacks are registered here; they are called from the UI task (Core 1).
    void init(std::function<void(const AppSettings&)>      onSettingsSaved,
              std::function<void(const TouchCalibration&)> onCalDone);

    // Build all screens from XML string (called from setup() on first boot with cached XML,
    // or from taskUI after receiving a RebuildLayout command).
    // Returns false if XML parse fails (falls back to a minimal error screen).
    bool buildFromXml(const char* xml, size_t len);

    // Build a minimal fallback UI (Settings + error notice) when no XML is available.
    void buildFallback(const char* errorMsg = nullptr);

    // ---- Public interface (called from main.cpp / tasks) ----
    void goToSettings();
    void goTo(PageId page);

    // Thread-safe post methods (called from taskNetwork / taskSensor, Core 0).
    // They enqueue a UiCommand; actual LVGL calls happen in tick().
    void postDataUpdate(DataPayload* payload);   // caller heap-allocs; SM frees after apply
    void postSensorUpdate(float temp, float hum, bool ok, float tvoc = 0, float co2 = 0);
    void postRebuildLayout(char* xml, size_t len); // caller ps_malloc's; SM ps_free's after build
    void postShowConnecting(const String& ssid);
    void postShowError(const String& msg);
    void postHideOverlay();
    void postGoToSettings();

    // Check and clear the pull-to-refresh request flag (polled by taskNetwork).
    bool consumeRefreshRequest();
    // Check and clear explicit Home message refresh request (Update tap).
    bool consumeHomeRefreshRequest();

    // Apply touch calibration immediately (called from setup() after NVS load).
    void applyCalibration(const TouchCalibration& cal);

    // Pre-fill settings fields (called from setup() after NVS load).
    void populateSettings(const AppSettings& settings);

    // LVGL handler + gesture + queue — call from taskUI every 5 ms.
    void tick();

    PlaceholderEngine& engine() { return _engine; }
    int8_t tzOffset() const { return _tzOffset; }
    void setTzOffset(int8_t tz) { _tzOffset = tz; }

private:
    // ---- Components ----
    PlaceholderEngine _engine;
    WidgetFactory     _factory;
    XmlParser         _parser;
    SettingsPage      _settingsPage;

    // ---- Screens ----
    lv_obj_t* _screens[4] = {};     // indexed by PageId
    PageId    _currentPage = PageId::Home;
    bool      _screensBuilt = false;

    // ---- Overlay ----
    lv_obj_t* _overlayScreen  = nullptr;
    lv_obj_t* _overlayMsg     = nullptr;
    lv_obj_t* _overlaySpinner = nullptr;
    lv_obj_t* _overlayBtn     = nullptr;
    bool      _overlayVisible = false;
    bool      _overlayGoSettingsOnDismiss = false;

    // ---- Pull-to-refresh ----
    lv_obj_t* _refreshSpinner = nullptr;
    bool      _refreshRequested = false;
    bool      _homeRefreshRequested = false;
    bool      _refreshSpinnerVisible = false;

    // ---- Gesture state ----
    lv_indev_t* _touchIndev = nullptr;  // set during init
    int32_t  _swipeAccX = 0, _swipeAccY = 0;
    int32_t  _swipeTouchX = 0, _swipeTouchY = 0;  // last raw touch position
    uint32_t _swipeStartMs = 0;
    bool     _swiping = false;
    bool     _indevWasPressed = false;

    // ---- RTC timer ----
    lv_timer_t* _rtcTimer    = nullptr;
    lv_timer_t* _minuteTimer = nullptr;
    lv_timer_t* _secondTimer = nullptr;
    int8_t      _tzOffset    = 0;

    // ---- FreeRTOS queue ----
    QueueHandle_t _queue;

    // ---- Callbacks ----
    std::function<void(const AppSettings&)>      _onSettingsSaved;
    std::function<void(const TouchCalibration&)> _onCalDone;

    // ---- Private methods ----
    void _createOverlay();
    void _showConnecting(const char* ssid);
    void _showError(const char* msg);
    void _hideOverlay();

    void _showRefreshSpinner();
    void _hideRefreshSpinner();

    void _processQueue();
    void _processGesture();

    void _navigateTo(PageId page, bool animate = true);

    static void _rtcTimerCb(lv_timer_t* t);
    static void _minuteTimerCb(lv_timer_t* t);
    static void _secondTimerCb(lv_timer_t* t);
    static void _onOverlayDismiss(lv_event_t* e);
};
