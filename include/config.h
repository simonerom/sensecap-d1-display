#pragma once

// =============================================================================
// DEFAULT WIFI CONFIGURATION (overridden by NVS settings)
// =============================================================================
#define WIFI_SSID_DEFAULT       "YourWiFiNetwork"
#define WIFI_PASSWORD_DEFAULT   "YourPassword"
#define WIFI_TIMEOUT_MS         15000

// =============================================================================
// DEFAULT HTTP ENDPOINT (overridden by NVS settings)
// =============================================================================
#define DATA_ENDPOINT_HOST_DEFAULT  "192.168.1.100"
#define DATA_ENDPOINT_PORT_DEFAULT  8765
#define DATA_ENDPOINT_PATH          "/data.json"
#define LAYOUT_ENDPOINT_PATH        "/layout.xml"

// Polling interval in milliseconds (default: 60 seconds)
#define POLL_INTERVAL_MS    60000

// HTTP timeout in milliseconds
#define HTTP_TIMEOUT_MS     5000

// Layout XML caching (SPIFFS)
#define LAYOUT_SPIFFS_PATH    "/layout.xml"   // file path in SPIFFS
#define LAYOUT_SPEC_VERSION   "1.0.0"         // expected spec major version

// =============================================================================
// GROVE SENSOR (I2C)
// =============================================================================
// SDA=13, SCL=15 for SenseCAP Indicator D1 Pro Grove port
#define GROVE_SDA_PIN       2
#define GROVE_SCL_PIN       3
#define SHT40_ADDR          0x44
#define DHT20_ADDR          0x38
#define SENSOR_POLL_MS      5000   // read sensor every 5 seconds

// =============================================================================
// TIMEZONE
// =============================================================================
#define TIMEZONE_OFFSET_DEFAULT  0   // UTC offset in hours (-12..+14)

// =============================================================================
// NVS KEYS
// =============================================================================
#define NVS_NAMESPACE       "sensecap"
#define NVS_KEY_SSID        "wifi_ssid"
#define NVS_KEY_PASS        "wifi_pass"
#define NVS_KEY_HOST        "srv_host"
#define NVS_KEY_PORT        "srv_port"
#define NVS_KEY_TZ          "timezone"
#define NVS_KEY_VALID       "configured"

// Touch calibration NVS keys
#define NVS_KEY_CAL_VALID   "cal_valid"
#define NVS_KEY_CAL_X0      "cal_x0"
#define NVS_KEY_CAL_X1      "cal_x1"
#define NVS_KEY_CAL_Y0      "cal_y0"
#define NVS_KEY_CAL_Y1      "cal_y1"

// Layout version cache (NVS key stores last-seen X-Layout-Version string)
#define NVS_KEY_LAYOUT_VER  "layout_ver"

// =============================================================================
// DISPLAY
// =============================================================================
#define SCREEN_WIDTH    480
#define SCREEN_HEIGHT   480
#define DISPLAY_ROTATION 2  // 180° rotation (panel mounted inverted)

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
#define COLOR_BG             0x1A1A2E  // Dark navy background
#define COLOR_PAGE1          0x1A1A2E  // Clock & Env page
#define COLOR_PAGE2          0x16213E  // Message page
#define COLOR_PAGE3          0x0F3460  // Settings page

#define COLOR_TEXT_PRIMARY   0xE0E0E0  // Primary text
#define COLOR_TEXT_SECONDARY 0x9E9E9E  // Secondary text
#define COLOR_ACCENT         0x00D4AA  // Teal accent (indoor)
#define COLOR_ALERT          0xFF4444  // Alert red
#define COLOR_OUTDOOR        0x74B9FF  // Outdoor blue
#define COLOR_WEATHER        0x74B9FF  // Weather blue
#define COLOR_TIME           0xFFFFFF  // Clock time white
#define COLOR_DOT_ACTIVE     0xFFFFFF  // Active navigation dot
#define COLOR_DOT_INACTIVE   0x555555  // Inactive navigation dot
#define COLOR_DIVIDER        0x333355  // Divider line

// Navigation dot sizes
#define NAV_DOT_SIZE    8
#define NAV_DOT_GAP     16

// Swipe gesture thresholds
#define SWIPE_MIN_DIST_PX    60    // minimum displacement px to count as swipe
#define SWIPE_MAX_MS         600   // swipe must complete within this time
#define SWIPE_AXIS_RATIO     2.0f  // primary axis must be 2x secondary to avoid diagonals

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
