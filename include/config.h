#pragma once

// =============================================================================
// CONFIGURAZIONE WIFI
// =============================================================================
#define WIFI_SSID       "TuaReteWiFi"
#define WIFI_PASSWORD   "TuaPassword"
#define WIFI_TIMEOUT_MS 10000

// =============================================================================
// ENDPOINT HTTP
// =============================================================================
// URL dell'endpoint locale che restituisce i dati JSON:
// { "date": "...", "message": "...", "weather": "...", "alert": "..." }
#define DATA_ENDPOINT_HOST  "192.168.1.100"
#define DATA_ENDPOINT_PORT  8080
#define DATA_ENDPOINT_PATH  "/api/display"

// Intervallo di polling in millisecondi (default: 60 secondi)
#define POLL_INTERVAL_MS    60000

// Timeout HTTP in millisecondi
#define HTTP_TIMEOUT_MS     5000

// =============================================================================
// DISPLAY
// =============================================================================
#define SCREEN_WIDTH    480
#define SCREEN_HEIGHT   320
#define DISPLAY_ROTATION 1  // Landscape

// Luminosita' backlight (0-255)
#define BACKLIGHT_BRIGHTNESS 200

// =============================================================================
// UI / LVGL
// =============================================================================
// Numero di pagine swipe
#define PAGE_COUNT      3

// Durata animazione swipe in ms
#define SWIPE_ANIM_MS   300

// Colori tema scuro
#define COLOR_BG        0x1A1A2E  // Sfondo scuro blu-notte
#define COLOR_PAGE1     0x16213E  // Pagina data/messaggio
#define COLOR_PAGE2     0x0F3460  // Pagina meteo
#define COLOR_PAGE3     0x1B1B2F  // Pagina alert

#define COLOR_TEXT_PRIMARY   0xE0E0E0  // Testo principale
#define COLOR_TEXT_SECONDARY 0x9E9E9E  // Testo secondario
#define COLOR_ACCENT         0x00D4AA  // Accento verde-acqua
#define COLOR_ALERT          0xFF6B6B  // Rosso alert
#define COLOR_WEATHER        0x74B9FF  // Azzurro meteo
#define COLOR_DOT_ACTIVE     0xFFFFFF  // Dot navigazione attivo
#define COLOR_DOT_INACTIVE   0x555555  // Dot navigazione inattivo

// Dimensioni dot navigazione
#define NAV_DOT_SIZE    8
#define NAV_DOT_GAP     16

// =============================================================================
// DEBUG
// =============================================================================
#define SERIAL_BAUD     115200
#define DEBUG_ENABLED   1

#if DEBUG_ENABLED
  #define DEBUG_PRINT(x)   Serial.print(x)
  #define DEBUG_PRINTLN(x) Serial.println(x)
  #define DEBUG_PRINTF(...)  Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_PRINT(x)
  #define DEBUG_PRINTLN(x)
  #define DEBUG_PRINTF(...)
#endif
