// =============================================================================
// main.cpp — SenseCAP Indicator D1 Pro Firmware (Dashboard v2)
//
// FreeRTOS tasks:
//   taskUI      (Core 1, prio 2) — LVGL tick + ScreenManager::tick()
//   taskNetwork (Core 0, prio 1) — WiFi, fetchLayout, fetchData
//   taskSensor  (Core 0, prio 1) — Grove sensor polling
//
// No shared mutex: cross-task communication via ScreenManager UI queue.
// =============================================================================
#include <Arduino.h>
#include <Wire.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <esp_timer.h>
#include <time.h>

#include "../include/config.h"
#include "ui.h"
#include "screen_manager.h"
#include "wifi_manager.h"
#include "data_fetcher.h"
#include "grove_sensor.h"
#include "rp2040_comm.h"
#include "settings_manager.h"

// =============================================================================
// Global instances
// =============================================================================
static ScreenManager   screenMgr;
static WiFiManager     wifiMgr;
static DataFetcher     fetcher;
static GroveSensor     sensor;
static RP2040Comm      rp2040;
static SettingsManager settingsMgr;
static AppSettings     appSettings;

// Timing
static uint32_t lastFetchMs  = 0;
static bool     firstFetch   = true;

// Top hardware button (single click: next page, double click: previous page)
static const int TOP_BUTTON_PIN = 38; // physical top button (detected via BTN_SCAN)
static bool topBtnLast = true;
static uint32_t topBtnLastChange = 0;
static uint32_t topBtnClickCount = 0;
static uint32_t topBtnFirstClickMs = 0;
static const uint32_t TOP_BUTTON_DBL_MS = 800;

static uint32_t heatFastUntilMs = 0;
static uint32_t lastHeatActionTs = 0;

// Layout version tracking (compared against X-Layout-Version header)
static String   cachedLayoutVersion = "";

// =============================================================================
// Settings callbacks (called from UI task — safe to access LVGL here via queue)
// =============================================================================
static void onSettingsSaved(const AppSettings& s) {
    DEBUG_PRINTLN("[Main] Settings saved, rebooting...");
    settingsMgr.save(s);
    delay(500);
    ESP.restart();
}

static void onCalibrationSaved(const TouchCalibration& cal) {
    settingsMgr.saveCalibration(cal);
    DEBUG_PRINTLN("[Main] Touch calibration saved to NVS.");
}

// =============================================================================
// Layout XML persistence helpers (SPIFFS)
// =============================================================================
static bool loadLayoutFromSpiffs(String& outXml) {
    if (!SPIFFS.exists(LAYOUT_SPIFFS_PATH)) return false;
    File f = SPIFFS.open(LAYOUT_SPIFFS_PATH, "r");
    if (!f) return false;
    outXml = f.readString();
    f.close();
    DEBUG_PRINTF("[Main] Loaded layout from SPIFFS (%u bytes).\n", outXml.length());
    return outXml.length() > 0;
}

static void saveLayoutToSpiffs(const char* xml, size_t len) {
    File f = SPIFFS.open(LAYOUT_SPIFFS_PATH, "w");
    if (!f) { DEBUG_PRINTLN("[Main] Failed to open SPIFFS for write."); return; }
    f.write((const uint8_t*)xml, len);
    f.close();
    DEBUG_PRINTF("[Main] Saved layout to SPIFFS (%u bytes).\n", len);
}

// =============================================================================
// FreeRTOS task: UI (Core 1)
// =============================================================================
void taskUI(void* pvParams) {
    for (;;) {
        screenMgr.tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// =============================================================================
// FreeRTOS task: Grove sensor (Core 0)
// =============================================================================
void taskSensor(void* pvParams) {
    // Init RP2040 UART communication (built-in sensors: AHT20, SCD41, SGP41)
    rp2040.begin();

    // Optionally probe external Grove T/H sensor
    GroveSensor::Type sType = sensor.begin(GROVE_SDA_PIN, GROVE_SCL_PIN);
    bool groveAvailable = (sType != GroveSensor::NONE);
    DEBUG_PRINTF("[Sensor] Grove type=%d available=%d\n", (int)sType, groveAvailable);

    static uint32_t lastMs = 0;
    for (;;) {
        // Always poll RP2040 UART (processes incoming bytes)
        rp2040.poll();

        uint32_t now = millis();

        // Top button click detection (debounced, count on PRESS edge)
        bool btn = digitalRead(TOP_BUTTON_PIN);
        if (btn != topBtnLast && (now - topBtnLastChange) > 35) {
            topBtnLastChange = now;
            topBtnLast = btn;

            // INPUT_PULLUP: pressed == LOW
            if (!btn) {
                if (topBtnClickCount == 0) {
                    topBtnClickCount = 1;
                    topBtnFirstClickMs = now;
                } else if (topBtnClickCount == 1) {
                    if ((now - topBtnFirstClickMs) <= TOP_BUTTON_DBL_MS) {
                        screenMgr.navigatePrevPageCyclic();
                        topBtnClickCount = 0;
                    } else {
                        // previous click expired; treat this as new first click
                        screenMgr.navigateNextPageCyclic();
                        topBtnClickCount = 1;
                        topBtnFirstClickMs = now;
                    }
                }
            }
        }
        // Single click: execute only after double-click window expires
        if (topBtnClickCount == 1 && (now - topBtnFirstClickMs) > TOP_BUTTON_DBL_MS) {
            screenMgr.navigateNextPageCyclic();
            topBtnClickCount = 0;
        }

        if (now - lastMs >= SENSOR_POLL_MS) {
            lastMs = now;

            RP2040Data rpData = rp2040.getData();

            // Use AHT20 from RP2040 as primary T/H source; fall back to Grove
            float t = 0, h = 0;
            bool ok = false;
            if (rpData.aht20_valid) {
                t  = rpData.aht20_temp;
                h  = rpData.aht20_hum;
                ok = true;
            } else if (groveAvailable) {
                ok = sensor.read(t, h);
            }

            float tvoc = rpData.tvoc_valid  ? rpData.tvoc      : 0;
            float co2  = rpData.scd41_valid ? rpData.scd41_co2 : 0;

            screenMgr.postSensorUpdate(t, h, ok, tvoc, co2);

            if (ok) {
                DEBUG_PRINTF("[Sensor] T=%.1fC  H=%.0f%%  tVOC=%.0f  CO2=%.0f ppm\n",
                             t, h, tvoc, co2);
            } else {
                DEBUG_PRINTLN("[Sensor] No T/H data");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// =============================================================================
// FreeRTOS task: Network (Core 0)
// =============================================================================
void taskNetwork(void* pvParams) {
    // Show connecting overlay
    screenMgr.postShowConnecting(appSettings.wifiSSID);

    DEBUG_PRINTF("[Net] Connecting WiFi: %s\n", appSettings.wifiSSID.c_str());
    bool connected = wifiMgr.connect(appSettings.wifiSSID.c_str(),
                                      appSettings.wifiPassword.c_str(),
                                      WIFI_TIMEOUT_MS);
    if (!connected) {
        DEBUG_PRINTLN("[Net] WiFi failed — go to Settings.");
        screenMgr.postHideOverlay();
        screenMgr.postGoToSettings();
        vTaskDelete(nullptr);
        return;
    }

    // Sync NTP time (needed for RTC placeholders)
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    DEBUG_PRINTLN("[Net] NTP sync started.");

    screenMgr.postHideOverlay();

    for (;;) {
        if (!wifiMgr.ensureConnected()) {
            vTaskDelay(pdMS_TO_TICKS(30000));
            continue;
        }

        uint32_t now = millis();
        bool pullRefresh = screenMgr.consumeRefreshRequest();
        bool homeRefresh = screenMgr.consumeHomeRefreshRequest();
        bool heatingEnterRefresh = screenMgr.consumeHeatingRefreshRequest();

        uint32_t pollMs = POLL_INTERVAL_MS;
        if (screenMgr.currentPage() == PageId::Heating) {
            pollMs = (now < heatFastUntilMs) ? 1000 : 10000;
        }

        bool shouldFetch = firstFetch ||
                           (now - lastFetchMs >= pollMs) ||
                           pullRefresh || homeRefresh || heatingEnterRefresh;

        if (shouldFetch) {
            if (homeRefresh) {
                DEBUG_PRINTLN("[Net] Home update tap: triggering /home/refresh...");
                if (!fetcher.triggerHomeRefresh()) {
                    DEBUG_PRINTF("[Net] /home/refresh failed: %s\n", fetcher.lastError().c_str());
                }
            }
            // ---- Fetch layout XML (only if version changed) ----
            String newVersion;
            size_t xmlLen = 0;
            char* xmlBuf = fetcher.fetchLayout(cachedLayoutVersion, newVersion, xmlLen);

            if (xmlBuf) {
                // New version received — save to SPIFFS and rebuild UI
                cachedLayoutVersion = newVersion;
                saveLayoutToSpiffs(xmlBuf, xmlLen);

                // Save version to NVS
                Preferences prefs;
                prefs.begin(NVS_NAMESPACE, false);
                prefs.putString(NVS_KEY_LAYOUT_VER, newVersion);
                prefs.end();

                screenMgr.postRebuildLayout(xmlBuf, xmlLen);
                // xmlBuf is freed by ScreenManager after rebuild
            } else if (!fetcher.lastError().isEmpty()) {
                DEBUG_PRINTF("[Net] Layout fetch error: %s\n", fetcher.lastError().c_str());
            }

            // ---- Fetch data.json ----
            DEBUG_PRINTLN("[Net] Fetching data.json...");
            DataPayload* payload = new DataPayload();
            if (fetcher.fetchData(*payload)) {
                auto it = payload->scalars.find("heat_action_ts");
                if (it != payload->scalars.end()) {
                    uint32_t ts = (uint32_t)it->second.toInt();
                    if (ts > lastHeatActionTs) {
                        lastHeatActionTs = ts;
                        heatFastUntilMs = now + 20000;
                    }
                }
                screenMgr.postDataUpdate(payload);
            } else {
                delete payload;
                DEBUG_PRINTF("[Net] Data fetch failed: %s\n", fetcher.lastError().c_str());
                if (firstFetch) {
                    screenMgr.postShowError("Data error:\n" + fetcher.lastError());
                    screenMgr.postGoToSettings();
                    // Stop network task to avoid bouncing back to fetch screen
                    vTaskDelete(nullptr);
                    return;
                }
            }

            lastFetchMs = now;
            firstFetch  = false;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// =============================================================================
// setup
// =============================================================================
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);
    DEBUG_PRINTLN("\n========================================");
    DEBUG_PRINTLN(" SenseCAP Indicator D1 Pro — v2");
    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTF(" Build: %s %s\n", __DATE__, __TIME__);
    DEBUG_PRINTF(" CPU: %d MHz  PSRAM: %d KB\n",
                 ESP.getCpuFreqMHz(), ESP.getPsramSize() / 1024);

    // SPIFFS for layout XML caching
    if (!SPIFFS.begin(true)) {
        DEBUG_PRINTLN("[Main] SPIFFS mount failed — reformatting...");
        SPIFFS.format();
        SPIFFS.begin(true);
    }

    // Load settings from NVS
    bool hasSettings = settingsMgr.load(appSettings);
    if (appSettings.wifiSSID == WIFI_SSID_DEFAULT) hasSettings = false;
    DEBUG_PRINTF("[Main] NVS settings: configured=%d\n", hasSettings);

    // Load cached layout version from NVS
    {
        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, true);
        cachedLayoutVersion = prefs.getString(NVS_KEY_LAYOUT_VER, "");
        prefs.end();
        DEBUG_PRINTF("[Main] Cached layout version: '%s'\n", cachedLayoutVersion.c_str());
    }

    // Hardware init
    DEBUG_PRINTLN("[Main] Init display...");
    lvgl_display_init();
    DEBUG_PRINTLN("[Main] Init touch...");
    lvgl_touch_init();
    lvgl_tick_timer_init();

    // I2C scan (after touch init, brief delay for serial monitor)
    delay(2000);
    DEBUG_PRINTLN("[I2C] Scanning SDA=39 SCL=40...");
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0)
            DEBUG_PRINTF("[I2C] Device at 0x%02X\n", addr);
    }
    DEBUG_PRINTLN("[I2C] Done.");

    // Init ScreenManager with callbacks
    screenMgr.init(onSettingsSaved, onCalibrationSaved);

    // Apply touch calibration from NVS
    TouchCalibration cal;
    if (settingsMgr.loadCalibration(cal)) {
        screenMgr.applyCalibration(cal);
        DEBUG_PRINTLN("[Main] Touch calibration applied.");
    }

    // Configure fetcher
    fetcher.configure(appSettings.serverHost, appSettings.serverPort, HTTP_TIMEOUT_MS);

    // Build UI from cached XML (fast start), or fallback if not cached
    String cachedXml;
    if (loadLayoutFromSpiffs(cachedXml) && cachedXml.length() > 0) {
        DEBUG_PRINTLN("[Main] Building UI from cached layout...");
        screenMgr.buildFromXml(cachedXml.c_str(), cachedXml.length());
    } else {
        DEBUG_PRINTLN("[Main] No cached layout, using fallback UI.");
        // Force re-fetch by clearing cached version so network task won't skip it
        cachedLayoutVersion = "";
        screenMgr.buildFallback("Fetching layout from server...");
    }

    // Pre-fill settings form
    screenMgr.populateSettings(appSettings);
    screenMgr.setTzOffset(appSettings.timezoneOffset);

    // Start FreeRTOS tasks
    xTaskCreatePinnedToCore(taskUI,     "UI",     12288, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(taskSensor, "Sensor", 4096,  nullptr, 1, nullptr, 0);

    if (!hasSettings) {
        DEBUG_PRINTLN("[Main] First boot — showing Settings.");
        screenMgr.postGoToSettings();
    } else {
        xTaskCreatePinnedToCore(taskNetwork, "Network", 20480, nullptr, 1, nullptr, 0);
    }

    DEBUG_PRINTLN("[Main] Tasks started.");
}

// =============================================================================
// loop (unused)
// =============================================================================
void loop() {
    vTaskDelay(pdMS_TO_TICKS(10000));
}
