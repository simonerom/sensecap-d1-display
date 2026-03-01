#include <Arduino.h>
#include <esp_timer.h>

#include "../include/config.h"
#include "wifi_manager.h"
#include "data_fetcher.h"
#include "ui.h"

// =============================================================================
// Istanze globali
// =============================================================================
static WiFiManager   wifiMgr;
static DataFetcher   fetcher(DATA_ENDPOINT_HOST, DATA_ENDPOINT_PORT, DATA_ENDPOINT_PATH, HTTP_TIMEOUT_MS);
static UIManager     ui;
static DisplayData   currentData;

// Timing polling
static uint32_t      lastFetchMs = 0;
static bool          firstFetch  = true;

// Mutex per accesso dati da task diversi
static SemaphoreHandle_t dataMutex = nullptr;

// =============================================================================
// Task FreeRTOS: UI (core 1)
// =============================================================================
void taskUI(void* pvParams) {
    for (;;) {
        // Aggiorna LVGL
        ui.tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// =============================================================================
// Task FreeRTOS: Network + Polling (core 0)
// =============================================================================
void taskNetwork(void* pvParams) {
    // Connessione WiFi iniziale
    DEBUG_PRINTLN("[Main] Connessione WiFi...");

    // Mostra schermata connessione
    if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
        ui.showConnecting();
        xSemaphoreGive(dataMutex);
    }

    bool connected = wifiMgr.connect(WIFI_SSID, WIFI_PASSWORD, WIFI_TIMEOUT_MS);

    if (!connected) {
        DEBUG_PRINTLN("[Main] WiFi non disponibile!");
        if (xSemaphoreTake(dataMutex, portMAX_DELAY) == pdTRUE) {
            ui.showError("WiFi non disponibile\n" WIFI_SSID);
            xSemaphoreGive(dataMutex);
        }
    }

    for (;;) {
        uint32_t now = millis();
        bool shouldFetch = firstFetch || (now - lastFetchMs >= POLL_INTERVAL_MS);

        if (shouldFetch && wifiMgr.ensureConnected()) {
            DEBUG_PRINTLN("[Main] Fetch dati...");
            DisplayData newData;
            bool ok = fetcher.fetch(newData);

            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (ok) {
                    currentData = newData;
                    ui.updateData(currentData);
                    DEBUG_PRINTLN("[Main] UI aggiornata.");
                } else {
                    DEBUG_PRINTF("[Main] Fetch fallito: %s\n", fetcher.lastError().c_str());
                    // Mantiene i dati precedenti, aggiorna solo lo stato
                    if (!currentData.valid) {
                        // Se non abbiamo ancora dati validi mostra errore
                        ui.showError("Errore dati:\n" + fetcher.lastError());
                    }
                }
                xSemaphoreGive(dataMutex);
            }

            lastFetchMs = now;
            firstFetch  = false;
        }

        // Controlla ogni secondo
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

    // Mutex per accesso dati condivisi
    dataMutex = xSemaphoreCreateMutex();

    // Inizializza display e LVGL
    DEBUG_PRINTLN("[Main] Init display LVGL...");
    lvgl_display_init();
    lvgl_tick_timer_init();

    // Inizializza UI
    DEBUG_PRINTLN("[Main] Init UI...");
    ui.init();

    // Avvia task su core separati
    xTaskCreatePinnedToCore(taskUI,      "UI",      8192,  nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(taskNetwork, "Network", 16384, nullptr, 1, nullptr, 0);

    DEBUG_PRINTLN("[Main] Task avviati.");
}

// =============================================================================
// Loop principale (non usato - tutto in task FreeRTOS)
// =============================================================================
void loop() {
    vTaskDelay(pdMS_TO_TICKS(10000));
}
