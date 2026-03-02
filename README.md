# SenseCAP Indicator D1 Pro — Dashboard Firmware

PlatformIO firmware for the **Seeed SenseCAP Indicator D1 Pro** (ESP32-S3 + 4" RGB touchscreen).

The device connects to WiFi, polls a local display server, and renders a fully customizable XML-driven dashboard with swipeable pages.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│  Mac (display server)         SenseCAP Indicator    │
│                                                     │
│  display_server.py  ──HTTP──▶  firmware (ESP32-S3) │
│   /layout.xml                   XML parser          │
│   /data.json                    widget factory      │
│   /health                       placeholder engine  │
│                                 LVGL renderer       │
└─────────────────────────────────────────────────────┘
```

- **Layout** (`/layout.xml`) — defines the UI structure; cached by firmware until `X-Layout-Version` header changes
- **Data** (`/data.json`) — dynamic values (weather, news, crypto…); refreshed on swipe-down
- **Pull-to-refresh** — swipe down on any page to reload data; if layout version changed, full UI reload

---

## Project Structure

```
sensecap-d1-display/
├── platformio.ini
├── include/
│   ├── config.h                # WiFi, server host/port, timeouts
│   └── lv_conf.h               # LVGL configuration
├── src/
│   ├── main.cpp                # Entry point, FreeRTOS tasks
│   ├── xml_parser.cpp          # XML layout parser
│   ├── widget_factory.cpp      # LVGL widget builder from XML
│   ├── placeholder_engine.cpp  # {placeholder} substitution + live updates
│   ├── data_fetcher.cpp        # HTTP fetch + JSON parse
│   ├── screen_manager.cpp      # Swipeable pages
│   ├── ui.cpp                  # Top-level UI init
│   ├── grove_sensor.cpp        # SGP40 (tVOC) + SCD41 (CO2) via I2C
│   ├── settings_manager.cpp
│   └── fonts/                  # Merged Montserrat + NotoSansSymbols2
├── scripts/
│   └── setup_lvgl.py           # Pre-build: patches LVGL include path
└── server/
    ├── display_server.py       # Python HTTP server (port 8765)
    └── server.log
```

---

## Hardware

| Component         | Specs                                                        |
|-------------------|--------------------------------------------------------------|
| Main MCU          | ESP32-S3 (240 MHz, WiFi + BT, 8MB flash, 8MB PSRAM OPI)    |
| Secondary MCU     | RP2040                                                       |
| Display           | 4" ST7701S RGB 480×480                                       |
| Touch             | FT5X06 capacitive                                            |
| I2C expander      | PCA9535 at 0x20 (SDA=39, SCL=40)                            |
| Sensors           | SGP40 (tVOC), SCD41 (CO2)                                   |

### USB Ports

| Port                        | Purpose              |
|-----------------------------|----------------------|
| `/dev/cu.wchusbserial2110`  | ESP32-S3 (flash/log) |
| `/dev/cu.usbmodem21201`     | RP2040               |

---

## Build & Flash

### Flash firmware (C++ changes only)

```bash
cd ~/Source/GitHub/simonerom/sensecap-d1-display
python3 -m platformio run -e sensecap_indicator --target upload --upload-port /dev/cu.wchusbserial2110
```

### Serial monitor

```bash
python3 -m platformio device monitor --port /dev/cu.usbmodem21201 --baud 115200
```

### When to flash vs swipe down

| Changed file                  | Action needed                                |
|-------------------------------|----------------------------------------------|
| `src/*.cpp` / `include/*.h`   | **Flash**                                    |
| `server/display_server.py`    | Restart server + swipe down                  |
| Layout XML (inside server)    | Bump `SPEC_VERSION` + restart server + swipe down |

> **Never flash for server or layout changes** — swipe down is enough.

---

## Display Server

The server runs on the Mac and serves layout + data to the device.

### Start / Stop

```bash
# Restart after editing display_server.py
launchctl unload ~/Library/LaunchAgents/com.simonerom.sensecap-server.plist
launchctl load ~/Library/LaunchAgents/com.simonerom.sensecap-server.plist
```

### Endpoints

| Endpoint          | Description                         |
|-------------------|-------------------------------------|
| `GET /layout.xml` | XML UI layout (cached by device)    |
| `GET /data.json`  | Dynamic data (refreshed on swipe)   |
| `GET /health`     | `{"status":"ok","version":"..."}`   |

Server: `http://192.168.1.29:8765/`

### Applying layout changes

1. Edit the XML layout inside `display_server.py`
2. Bump `SPEC_VERSION` (e.g. `"1.3.6"` → `"1.3.7"`) **and** the `<screens version="...">` attribute
3. Restart the server
4. Swipe down on the device

---

## XML Layout

Pages are defined in `display_server.py` inside the `LAYOUT_XML` string.

### Elements

| Element        | Description                                                                                   |
|----------------|-----------------------------------------------------------------------------------------------|
| `<screens>`    | Root container, `version` attribute                                                           |
| `<screen>`     | One swipeable page, `bg` color                                                                |
| `<card>`       | Rounded container: `bg`, `radius`, `pad`, `pad_h`, `pad_v`, `gap`, `tight`, `flex`, `h`, `scroll`, `valign` |
| `<row>`        | Horizontal flex container: `gap`, `h`                                                         |
| `<col>`        | Vertical flex container: `gap`, `flex`                                                        |
| `<label>`      | Text label: `text`, `font`, `bold`, `color`, `align`, `max_lines`, `flex`, `w`, `visible`    |
| `<list>`       | Bulleted list from array placeholder: `items`, `font`, `color`, `divider`, `max_lines`       |
| `<crypto_row>` | Coin row: `symbol`, `price`, `change`, `trend`, `up_color`, `down_color`                     |

### Placeholders

Any `{key}` in a `text` attribute is replaced live from `data.json`.  
Arrays (`{news_italia}`, `{scioperi}`, etc.) are used with `<list items="{key}"/>`.

---

## data.json Reference

| Field                | Type   | Description                                      |
|----------------------|--------|--------------------------------------------------|
| `indoor_temp`        | string | Indoor temperature (SHT40)                       |
| `indoor_hum`         | string | Indoor humidity                                  |
| `outdoor_temp`       | string | Outdoor temperature (Open-Meteo)                 |
| `voc`                | string | tVOC index (SGP40) or `--`                       |
| `co2`                | string | CO2 ppm (SCD41) or `--`                          |
| `day_name`           | string | Weekday in Italian uppercase (e.g. `LUNEDI`)     |
| `day_num`            | string | Day of month                                     |
| `month_name`         | string | Full Italian month name (e.g. `Marzo`)           |
| `day_color`          | string | `#E53935` on Sunday/holidays, dark otherwise     |
| `meteo_summary`      | string | Multi-line weather + forecast + rain info        |
| `scioperi`           | array  | Upcoming ATM/transit strikes                     |
| `scioperi_text`      | string | Same, newline-joined (for single label)          |
| `scioperi_visible`   | string | `"true"` / `"false"`                             |
| `news_italia`        | array  | Italian news (ANSA)                              |
| `news_estero`        | array  | International news (Google News IT)              |
| `news_milano`        | array  | Milan local news                                 |
| `btc_price/change/trend` | string | Bitcoin                                    |
| `eth_price/change/trend` | string | Ethereum                                   |
| `iotx_price/change/trend`| string | IoTeX                                      |
| `curiosity`          | string | Daily curiosity fact                             |

---

## Fonts

Merged Montserrat + NotoSansSymbols2, compiled in `src/fonts/`.

| Function                     | Description                                      |
|------------------------------|--------------------------------------------------|
| `lv_hlp_font(size)`          | Regular (sizes: 12,14,18,22,24,28,32,48,64,96)  |
| `lv_hlp_font_bold(size)`     | Bold (sizes: 18,24,28,32,96,192)                 |
| `lv_hlp_font_ex(size, bold)` | Auto-selects variant                             |

Font 192 bold contains digits only (used for large day-of-month number).  
Supported symbols: `★ ▲ ▶ ◆ ◉ ☀ ☁ ⚠ ⚡ ° •`

---

## Dependencies

| Library                 | Version   |
|-------------------------|-----------|
| `lvgl/lvgl`             | ^8.3.11   |
| `bblanchon/ArduinoJson` | ^7.0.4    |
| `Arduino_GFX`           | (RGB display driver) |

---

## License

MIT
