# SenseCAP Indicator D1 - Display Firmware

PlatformIO firmware for the **Seeed SenseCAP Indicator D1** (ESP32-S3 + 4-inch touchscreen display).

The device connects to WiFi, polls a local HTTP endpoint every 60 seconds, and displays the data on an LVGL interface with 3 swipeable pages.

---

## Features

- **Automatic WiFi connection** with reconnection on signal loss
- **HTTP polling** every 60 seconds from a configurable local endpoint
- **LVGL UI** with 3 horizontally swipeable pages:
  - Page 1: Date and message
  - Page 2: Weather conditions
  - Page 3: Alerts/notifications
- **Dark theme** with large fonts and navigation indicators (dots)
- **Multi-task FreeRTOS architecture** (UI on core 1, network on core 0)
- **Automatic refresh** every 60 seconds

---

## Project Structure

```
sensecap-d1-display/
├── platformio.ini          # PlatformIO configuration
├── include/
│   ├── config.h            # WiFi, endpoint, and color configuration
│   └── lv_conf.h           # LVGL configuration
├── src/
│   ├── main.cpp            # Entry point and FreeRTOS tasks
│   ├── wifi_manager.h/.cpp # WiFi connection management
│   ├── data_fetcher.h/.cpp # HTTP/JSON data fetching and parsing
│   └── ui.h/.cpp           # LVGL UI (3 pages + overlay)
├── scripts/
│   └── setup_lvgl.py       # Pre-build script to configure LVGL
└── README.md
```

---

## Configuration

Edit `include/config.h` before building:

```c
// WiFi
#define WIFI_SSID       "YourWiFiNetwork"
#define WIFI_PASSWORD   "YourPassword"

// Local HTTP endpoint
#define DATA_ENDPOINT_HOST  "192.168.1.100"
#define DATA_ENDPOINT_PORT  8080
#define DATA_ENDPOINT_PATH  "/api/display"

// Polling interval (milliseconds)
#define POLL_INTERVAL_MS    60000
```

---

## API Response Format

The endpoint must return a JSON object with this schema:

```json
{
  "date":    "Sunday, March 1, 2026 - 10:30",
  "message": "Good morning! You have 3 new messages.",
  "weather": "Sunny, 18°C - Wind 10 km/h",
  "alert":   "Meeting at 3:00 PM in the conference room"
}
```

| Field     | Type   | Description                                           |
|-----------|--------|-------------------------------------------------------|
| `date`    | string | Date and time shown on page 1                         |
| `message` | string | Main message shown on page 1                          |
| `weather` | string | Weather conditions shown on page 2                    |
| `alert`   | string | Alert text shown on page 3 (`""` = no active alert)   |

---

## Dependencies

| Library                   | Version  | Description              |
|---------------------------|----------|--------------------------|
| `lvgl/lvgl`               | ^8.3.11  | GUI library              |
| `bodmer/TFT_eSPI`         | ^2.5.43  | SPI display driver       |
| `bblanchon/ArduinoJson`   | ^7.0.4   | JSON parsing             |

---

## Build & Flash

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- Python 3.x (for the pre-build script)

### Compilation

```bash
# From the command line
pio run

# Flash to device
pio run --target upload

# Serial monitor
pio device monitor
```

### First Build

1. Clone the repository
2. Edit `include/config.h` with your WiFi credentials and endpoint
3. Connect the SenseCAP Indicator D1 via USB
4. Run `pio run --target upload`

---

## Hardware: SenseCAP Indicator D1

| Component        | Specs                              |
|------------------|------------------------------------|
| Main MCU         | ESP32-S3 (240 MHz, WiFi + BT)     |
| Secondary MCU    | RP2040 (not used in this firmware) |
| Display          | 4" IPS touchscreen 480x320        |
| Display driver   | ILI9341 (SPI)                     |
| Touch            | Resistive (calibration included)  |
| Flash            | 8 MB                              |
| PSRAM            | 8 MB (OSPI)                       |

---

## Display Pins (ESP32-S3)

| Function  | GPIO |
|-----------|------|
| MOSI      | 11   |
| SCLK      | 12   |
| CS        | 10   |
| DC        | 14   |
| RST       | 9    |
| Backlight | 45   |
| Touch CS  | 8    |

---

## Firmware Architecture

```
setup()
 ├── lvgl_display_init()    # Init TFT_eSPI + LVGL driver
 ├── lvgl_tick_timer_init() # 5ms ISR timer for LVGL
 ├── ui.init()              # Create the 3 LVGL pages
 ├── taskUI (Core 1)        # LVGL loop every 5ms
 └── taskNetwork (Core 0)   # WiFi + fetch every 60s
```

### Data Flow

```
taskNetwork:
  wifiMgr.ensureConnected()
    └─> WiFi.begin() / reconnect
  fetcher.fetch(data)
    └─> HTTP GET -> JSON parse -> DisplayData
  ui.updateData(data)
    └─> update LVGL labels

taskUI:
  ui.tick()
    └─> lv_timer_handler()  # Redraw if needed
```

---

## UI Customization

Colors are defined in `include/config.h`:

```c
#define COLOR_BG        0x1A1A2E  // Main background
#define COLOR_PAGE1     0x16213E  // Page 1 background
#define COLOR_ACCENT    0x00D4AA  // Accent color (teal)
#define COLOR_ALERT     0xFF6B6B  // Alert color (red)
#define COLOR_WEATHER   0x74B9FF  // Weather color (blue)
```

---

## Example Server (Python)

A simple Flask server for testing the firmware:

```python
from flask import Flask, jsonify
from datetime import datetime

app = Flask(__name__)

@app.route('/api/display')
def display_data():
    return jsonify({
        "date":    datetime.now().strftime("%A %d %B %Y - %H:%M"),
        "message": "System running. Everything is normal.",
        "weather": "Sunny, 18°C - Humidity 65%",
        "alert":   ""  # Empty string = no active alert
    })

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8080)
```

Start with: `python server.py`

---

## Touch Calibration

The resistive touch calibration values are in `src/ui.cpp`:

```cpp
uint16_t calData[5] = {275, 3620, 264, 3532, 1};
tft.setTouchCalibrate(calData);
```

To recalibrate, run the TFT_eSPI calibration sketch and replace the values.

---

## License

MIT License - see [LICENSE](LICENSE) for details.
