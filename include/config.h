#pragma once

// =============================================================================
// WIFI CONFIGURATION
// =============================================================================
#define WIFI_SSID       "YourWiFiNetwork"
#define WIFI_PASSWORD   "YourPassword"
#define WIFI_TIMEOUT_MS 10000

// =============================================================================
// HTTP ENDPOINT
// =============================================================================
// URL of the local endpoint that returns JSON data:
// { "date": "...", "message": "...", "weather": "...", "alert": "..." }
#define DATA_ENDPOINT_HOST  "192.168.1.100"
#define DATA_ENDPOINT_PORT  8080
#define DATA_ENDPOINT_PATH  "/api/display"

// Polling interval in milliseconds (default: 60 seconds)
#define POLL_INTERVAL_MS    60000

// HTTP timeout in milliseconds
#define HTTP_TIMEOUT_MS     5000

// =============================================================================
// DISPLAY
// =============================================================================
#define SCREEN_WIDTH    480
#define SCREEN_HEIGHT   320
#define DISPLAY_ROTATION 1  // Landscape

// Backlight brightness (0-255)
#define BACKLIGHT_BRIGHTNESS 200

// =============================================================================
// UI / LVGL
// =============================================================================
// Number of swipe pages
#define PAGE_COUNT      3

// Swipe animation duration in ms
#define SWIPE_ANIM_MS   300

// Dark theme colors
#define COLOR_BG        0x1A1A2E  // Dark navy background
#define COLOR_PAGE1     0x16213E  // Date/message page
#define COLOR_PAGE2     0x0F3460  // Weather page
#define COLOR_PAGE3     0x1B1B2F  // Alert page

#define COLOR_TEXT_PRIMARY   0xE0E0E0  // Primary text
#define COLOR_TEXT_SECONDARY 0x9E9E9E  // Secondary text
#define COLOR_ACCENT         0x00D4AA  // Teal accent
#define COLOR_ALERT          0xFF6B6B  // Alert red
#define COLOR_WEATHER        0x74B9FF  // Weather blue
#define COLOR_DOT_ACTIVE     0xFFFFFF  // Active navigation dot
#define COLOR_DOT_INACTIVE   0x555555  // Inactive navigation dot

// Navigation dot sizes
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
