#include <Arduino.h>
#include <esp_timer.h>

#include "../include/config.h"
#include "wifi_manager.h"
#include "data_fetcher.h"
#include "ui.h"

// =============================================================================
// Global instances
// =============================================================================
static WiFiManager   wifiMgr;
static DataFetcher   fetcher(DATA_ENDPOINT_HOST, DATA_ENDPOINT_PORT, DATA_ENDPOINT_PATH, HTTP_TIMEOUT_MS);
static UIManager     ui;
static DisplayData   currentData;

// Timing polling
static uint32_t      lastFetchMs = 0;
static bool          firstFetch  = true;

// Mutex for shared data access across tasks
static SemaphoreHandle_t dataMutex = nullptr;

// =============================================================================
// FreeRTOS task: UI (core 1)
// =============================================================================
void taskUI(void* pvParams) {
    for (;;) {
        // Update LVGL
        ui.tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// =============================================================================
// FreeRTOS task: Network + Polling (core 0)
// =============================================================================
void taskNetwork(void* pvParams) {
    // Initial WiFi connection
    DEBUG_PRINTLN("[Main] Connecting to WiFi...");

    // Show connection screen
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        ui.showConnecting();
        xSemaphoreGive(dataMutex);
    }

    bool connected = wifiMgr.connect(WIFI_SSID, WIFI_PASSWORD, WIFI_TIMEOUT_MS);

    if (!connected) {
        DEBUG_PRINTLN("[Main] WiFi not available!");
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            ui.showError("WiFi not available\n" WIFI_SSID);
            xSemaphoreGive(dataMutex);
        }
    }

    for (;;) {
        uint32_t now = millis();
        bool shouldFetch = firstFetch || (now - lastFetchMs >= POLL_INTERVAL_MS);

        if (shouldFetch && wifiMgr.ensureConnected()) {
            DEBUG_PRINTLN("[Main] Fetching data...");
            DisplayData newData;
            bool ok = fetcher.fetch(newData);

            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (ok) {
                    currentData = newData;
                    ui.updateData(currentData);
                    DEBUG_PRINTLN("[Main] UI updated.");
                } else {
                    DEBUG_PRINTF("[Main] Fetch failed: %s\n", fetcher.lastError().c_str());
                    // Keep previous data, update status only
                    if (!currentData.valid) {
                        // No valid data yet, show error
                        ui.showError("Data error:\n" + fetcher.lastError());
                    }
                }
                xSemaphoreGive(dataMutex);
            }

            lastFetchMs = now;
            firstFetch  = false;
        }

        // Check every second
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
    DEBUG_PRINTLN(" SenseCAP Indicator D1 - Display Firmware");
    DEBUG_PRINTLN("========================================");
    DEBUG_PRINTF(" Build: %s %s\n", __DATE__, __TIME__);
    DEBUG_PRINTF(" CPU: %d MHz  PSRAM: %d KB\n",
                 ESP.getCpuFreqMHz(),
                 ESP.getPsramSize() / 1024);

    // Mutex for shared data access
    dataMutex = xSemaphoreCreateMutex();

    // Initialize display and LVGL
    DEBUG_PRINTLN("[Main] Init display LVGL...");
    lvgl_display_init();
    lvgl_tick_timer_init();

    // Initialize UI
    DEBUG_PRINTLN("[Main] Init UI...");
    ui.init();

    // Start tasks on separate cores
    xTaskCreatePinnedToCore(taskUI,      "UI",      8192,  nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(taskNetwork, "Network", 16384, nullptr, 1, nullptr, 0);

    DEBUG_PRINTLN("[Main] Tasks started.");
}

// =============================================================================
// Main loop (unused - everything runs in FreeRTOS tasks)
// =============================================================================
void loop() {
    vTaskDelay(pdMS_TO_TICKS(10000));
}
