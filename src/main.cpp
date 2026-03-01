#include <Arduino.h>
#include <esp_timer.h>

#include "../include/config.h"
#include "wifi_manager.h"
#include "data_fetcher.h"
#include "grove_sensor.h"
#include "settings_manager.h"
#include "ui.h"

// =============================================================================
// Global instances
// =============================================================================
static WiFiManager     wifiMgr;
static DataFetcher     fetcher;
static UIManager       ui;
static GroveSensor     sensor;
static SettingsManager settingsMgr;
static AppSettings     appSettings;

static DisplayData     currentData;

// Timing
static uint32_t lastFetchMs  = 0;
static uint32_t lastSensorMs = 0;
static bool     firstFetch   = true;

// Mutex for shared data access across tasks
static SemaphoreHandle_t dataMutex = nullptr;

// =============================================================================
// Settings save callback (called from UI task when user taps Save)
// =============================================================================
static void onSettingsSaved(const AppSettings& newSettings) {
    DEBUG_PRINTLN("[Main] Settings saved, rebooting...");
    settingsMgr.save(newSettings);
    delay(500);
    ESP.restart();
}

// =============================================================================
// FreeRTOS task: UI (core 1)
// =============================================================================
void taskUI(void* pvParams) {
    for (;;) {
        ui.tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// =============================================================================
// FreeRTOS task: Grove sensor polling (core 0)
// =============================================================================
void taskSensor(void* pvParams) {
    // Initialize Grove I2C sensor
    GroveSensor::Type sType = sensor.begin(GROVE_SDA_PIN, GROVE_SCL_PIN);
    bool available = (sType != GroveSensor::NONE);

    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        if (available) {
            currentData.sensorAvailable = false; // will be true after first read
        } else {
            currentData.sensorAvailable = false;
            ui.updateSensor(0, 0, false);
        }
        xSemaphoreGive(dataMutex);
    }

    for (;;) {
        uint32_t now = millis();
        if (now - lastSensorMs >= SENSOR_POLL_MS) {
            lastSensorMs = now;

            if (available) {
                float t = 0, h = 0;
                bool ok = sensor.read(t, h);
                if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    currentData.tempIndoor     = ok ? t : 0;
                    currentData.humidityIndoor = ok ? h : 0;
                    currentData.sensorAvailable = ok;
                    ui.updateSensor(t, h, ok);
                    xSemaphoreGive(dataMutex);
                }
                if (ok) {
                    DEBUG_PRINTF("[Sensor] T=%.1f°C  H=%.0f%%\n", t, h);
                } else {
                    DEBUG_PRINTLN("[Sensor] Read failed");
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// =============================================================================
// FreeRTOS task: Network + polling (core 0)
// =============================================================================
void taskNetwork(void* pvParams) {
    // Show WiFi connecting overlay
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        ui.showConnecting(appSettings.wifiSSID);
        xSemaphoreGive(dataMutex);
    }

    DEBUG_PRINTF("[Main] Connecting WiFi: %s\n", appSettings.wifiSSID.c_str());
    bool connected = wifiMgr.connect(appSettings.wifiSSID.c_str(),
                                      appSettings.wifiPassword.c_str(),
                                      WIFI_TIMEOUT_MS);

    if (!connected) {
        DEBUG_PRINTLN("[Main] WiFi connection failed!");
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            ui.showError("WiFi not available\n" + appSettings.wifiSSID +
                         "\n\nCheck Settings (gear icon)");
            xSemaphoreGive(dataMutex);
        }
    } else {
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            ui.hideOverlay();
            xSemaphoreGive(dataMutex);
        }
    }

    for (;;) {
        uint32_t now = millis();
        bool shouldFetch = firstFetch || (now - lastFetchMs >= POLL_INTERVAL_MS);

        if (shouldFetch && wifiMgr.ensureConnected()) {
            DEBUG_PRINTLN("[Main] Fetching data...");
            DisplayData newData;
            // Preserve sensor values across fetch
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                newData.tempIndoor      = currentData.tempIndoor;
                newData.humidityIndoor  = currentData.humidityIndoor;
                newData.sensorAvailable = currentData.sensorAvailable;
                xSemaphoreGive(dataMutex);
            }

            bool ok = fetcher.fetch(newData);

            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (ok) {
                    currentData = newData;
                    ui.updateData(currentData);
                    DEBUG_PRINTLN("[Main] UI updated.");
                } else {
                    DEBUG_PRINTF("[Main] Fetch failed: %s\n", fetcher.lastError().c_str());
                    if (!currentData.valid) {
                        ui.showError("Data error:\n" + fetcher.lastError());
                    }
                }
                xSemaphoreGive(dataMutex);
            }

            lastFetchMs = now;
            firstFetch  = false;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// =============================================================================
// Setup
// =============================================================================
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);
    DEBUG_PRINTLN("\n========================================");
    DEBUG_PRINTLN(" SenseCAP Indicator D1 Pro - Firmware");
    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTF(" Build: %s %s\n", __DATE__, __TIME__);
    DEBUG_PRINTF(" CPU: %d MHz  PSRAM: %d KB\n",
                 ESP.getCpuFreqMHz(),
                 ESP.getPsramSize() / 1024);

    // Mutex for shared data access
    dataMutex = xSemaphoreCreateMutex();

    // Load settings from NVS
    bool hasSettings = settingsMgr.load(appSettings);
    DEBUG_PRINTF("[Main] NVS settings loaded: configured=%d\n", hasSettings);

    // Initialize display and LVGL
    DEBUG_PRINTLN("[Main] Init display LVGL...");
    lvgl_display_init();
    lvgl_tick_timer_init();

    // Initialize UI with settings-save callback
    DEBUG_PRINTLN("[Main] Init UI...");
    ui.init(onSettingsSaved);

    // Pre-fill settings page with current values
    ui.populateSettings(appSettings);

    // Configure data fetcher with saved settings
    fetcher.configure(appSettings.serverHost,
                      appSettings.serverPort,
                      DATA_ENDPOINT_PATH,
                      HTTP_TIMEOUT_MS);

    // If no settings saved yet, redirect to settings page
    if (!hasSettings) {
        DEBUG_PRINTLN("[Main] First boot - redirecting to Settings page.");
        ui.goToSettings();
    }

    // Start FreeRTOS tasks
    xTaskCreatePinnedToCore(taskUI,      "UI",      8192,  nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(taskNetwork, "Network", 16384, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(taskSensor,  "Sensor",  4096,  nullptr, 1, nullptr, 0);

    DEBUG_PRINTLN("[Main] Tasks started.");
}

// =============================================================================
// Main loop (unused)
// =============================================================================
void loop() {
    vTaskDelay(pdMS_TO_TICKS(10000));
}
