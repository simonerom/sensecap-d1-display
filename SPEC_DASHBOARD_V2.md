# SenseCAP Indicator D1 Pro — Dashboard v2 Spec

## Overview

Redesign the firmware UI with a server-driven layout system.
The device fetches layout + data from an OpenClaw HTTP server and renders it using LVGL 8.

---

## Architecture

### Two endpoints on the server (http://SERVER_IP:PORT):

**GET /layout.xml** — page structure (changes rarely, cached on device)
**GET /data.json** — live values (fetched every 60s or on pull-to-refresh)

### Separation of concerns:
- Server controls layout and style
- Device renders LVGL widgets from XML + substitutes {placeholders} with data.json values
- Live placeholders ({time}, {indoor_temp}, {indoor_hum}) update locally without server roundtrip

---

## Page Navigation

```
[Settings] ←swipe left← [Home] →swipe right→ [Calendar] →swipe right→ [Clock]
```

- Swipe down on any page = pull-to-refresh (reload data.json, animate spinner)
- Settings accessible from swipe left on Home only

---

## Mini XML Layout Language

The server sends an XML file describing the UI. The firmware parses it and builds LVGL widgets.

### Supported elements:

```xml
<screen bg="#1A1A2E" id="home|calendar|clock|settings">

  <!-- Full-width card with optional vertical scroll -->
  <card bg="#16213E" radius="16" pad="16" w="100%" scroll="true|false">
    
    <!-- Text label -->
    <label text="{placeholder}" font="28" color="#FFFFFF" align="left|center|right" bold="true"/>
    
    <!-- Horizontal row of children -->
    <row gap="12">
      <card flex="1" bg="#16213E" radius="16" pad="12">
        ...
      </card>
    </row>
    
    <!-- Vertical list of text items from array placeholder -->
    <list items="{news}" font="14" color="#CCCCCC" divider="#333355" max_lines="2"/>
    
    <!-- Crypto ticker row: symbol + price + change colored up/down -->
    <crypto_row symbol="{btc_symbol}" price="{btc_price}" change="{btc_change}" trend="{btc_trend}"/>
    
    <!-- Calendar grid widget — renders month view, highlights today -->
    <calendar_grid year="{cal_year}" month="{cal_month}" today="{cal_today}"/>
    
    <!-- Events list from array -->
    <events_list items="{events}" font="16" color="#FFFFFF"/>
    
    <!-- Big clock — updates every second locally, no server needed -->
    <big_clock font="96" color="#FFFFFF" align="center"/>
    
    <!-- Settings form — built-in, not from XML -->
    <settings_form/>

  </card>

</screen>
```

### Placeholder types:

| Placeholder | Source | Update frequency |
|-------------|--------|-----------------|
| `{time}` | Local RTC | Every second |
| `{date}` | Local RTC | Every minute |
| `{day}` | Local RTC | Every minute |
| `{weekday}` | Local RTC | Every minute |
| `{indoor_temp}` | Grove sensor | Every 5s |
| `{indoor_hum}` | Grove sensor | Every 5s |
| `{outdoor_temp}` | data.json | On fetch |
| `{outdoor_hum}` | data.json | On fetch |
| `{btc_price}` | data.json | On fetch |
| `{btc_change}` | data.json | On fetch |
| `{btc_trend}` | data.json | On fetch (up/down) |
| `{eth_price}` | data.json | On fetch |
| `{eth_change}` | data.json | On fetch |
| `{iotx_price}` | data.json | On fetch |
| `{iotx_change}` | data.json | On fetch |
| `{news}` | data.json | On fetch (array) |
| `{events}` | data.json | On fetch (array) |
| `{message}` | data.json | On fetch |
| `{curiosity}` | data.json | On fetch |
| `{cal_year}` | Local RTC | Daily |
| `{cal_month}` | Local RTC | Daily |
| `{cal_today}` | Local RTC | Daily |
| `{alert}` | data.json | On fetch |

---

## data.json schema

```json
{
  "outdoor_temp": "12°C",
  "outdoor_hum": "78%",
  "condition": "Nuvoloso",
  "btc_symbol": "BTC", "btc_price": "$66.298", "btc_change": "-0.3%", "btc_trend": "down",
  "eth_symbol": "ETH", "eth_price": "$1.948", "eth_change": "-1.8%", "eth_trend": "down",
  "iotx_symbol": "IOTX", "iotx_price": "$0.0046", "iotx_change": "+5.4%", "iotx_trend": "up",
  "news": [
    "Tensioni nel Golfo Persico: prezzi petrolio in rialzo",
    "Germania: accordo di governo raggiunto",
    "Milano: sciopero ATM mercoledì 8-12"
  ],
  "events": [
    {"date": "2 mar", "time": "09:00", "title": "Riunione team"},
    {"date": "9 mar", "time": "08:30", "title": "Ecografia Fertilab"}
  ],
  "message": "Buongiorno! Oggi il meteo è clemente...",
  "curiosity": "Il 2 marzo 1969 volò per la prima volta il Concorde ✈️",
  "alert": ""
}
```

---

## layout.xml example

```xml
<screens>

  <screen id="home" bg="#1A1A2E">
    <card bg="#16213E" radius="16" pad="20" w="100%" scroll="false">
      <label text="{day}" font="96" color="#FFFFFF" align="center" bold="true"/>
      <label text="{weekday}" font="28" color="#00D4AA" align="center"/>
    </card>
    <row gap="12" pad="16">
      <card flex="1" bg="#16213E" radius="16" pad="12">
        <label text="🌡️ Interno" font="13" color="#AAAAAA" align="center"/>
        <label text="{indoor_temp}" font="28" color="#00D4AA" align="center" bold="true"/>
        <label text="{indoor_hum}" font="14" color="#AAAAAA" align="center"/>
      </card>
      <card flex="1" bg="#16213E" radius="16" pad="12">
        <label text="🌤️ Esterno" font="13" color="#AAAAAA" align="center"/>
        <label text="{outdoor_temp}" font="28" color="#74B9FF" align="center" bold="true"/>
        <label text="{outdoor_hum}" font="14" color="#AAAAAA" align="center"/>
      </card>
    </row>
    <card bg="#16213E" radius="16" pad="16" w="100%" scroll="true">
      <label text="☀️ Buongiorno" font="20" color="#FFFFFF" bold="true"/>
      <label text="{message}" font="15" color="#CCCCCC"/>
      <label text="🌍 Curiosità" font="18" color="#FFFFFF" bold="true"/>
      <label text="{curiosity}" font="14" color="#CCCCCC"/>
      <label text="💰 Crypto" font="18" color="#FFFFFF" bold="true"/>
      <crypto_row symbol="{btc_symbol}" price="{btc_price}" change="{btc_change}" trend="{btc_trend}"/>
      <crypto_row symbol="{eth_symbol}" price="{eth_price}" change="{eth_change}" trend="{eth_trend}"/>
      <crypto_row symbol="{iotx_symbol}" price="{iotx_price}" change="{iotx_change}" trend="{iotx_trend}"/>
      <label text="📰 Notizie" font="18" color="#FFFFFF" bold="true"/>
      <list items="{news}" font="14" color="#CCCCCC" divider="#333355" max_lines="2"/>
    </card>
    <card bg="#FF6B6B" radius="12" pad="12" w="100%" visible="{alert_visible}">
      <label text="⚠️ {alert}" font="16" color="#FFFFFF" align="center"/>
    </card>
  </screen>

  <screen id="calendar" bg="#1A1A2E">
    <calendar_grid year="{cal_year}" month="{cal_month}" today="{cal_today}"/>
    <card bg="#16213E" radius="16" pad="16" w="100%" scroll="true">
      <label text="📅 Prossimi eventi" font="18" color="#FFFFFF" bold="true"/>
      <events_list items="{events}" font="15" color="#FFFFFF"/>
    </card>
    <row gap="12" pad="16">
      <card flex="1" bg="#16213E" radius="16" pad="12">
        <label text="🌡️ Interno" font="13" color="#AAAAAA" align="center"/>
        <label text="{indoor_temp}" font="24" color="#00D4AA" align="center" bold="true"/>
      </card>
      <card flex="1" bg="#16213E" radius="16" pad="12">
        <label text="🌤️ Esterno" font="13" color="#AAAAAA" align="center"/>
        <label text="{outdoor_temp}" font="24" color="#74B9FF" align="center" bold="true"/>
      </card>
    </row>
  </screen>

  <screen id="clock" bg="#1A1A2E">
    <big_clock font="96" color="#FFFFFF" align="center"/>
    <label text="{weekday} {day} {month}" font="22" color="#AAAAAA" align="center"/>
    <row gap="12" pad="16">
      <card flex="1" bg="#16213E" radius="16" pad="16">
        <label text="🌡️ Interno" font="13" color="#AAAAAA" align="center"/>
        <label text="{indoor_temp}" font="32" color="#00D4AA" align="center" bold="true"/>
        <label text="{indoor_hum}" font="16" color="#AAAAAA" align="center"/>
      </card>
      <card flex="1" bg="#16213E" radius="16" pad="16">
        <label text="🌤️ Esterno" font="13" color="#AAAAAA" align="center"/>
        <label text="{outdoor_temp}" font="32" color="#74B9FF" align="center" bold="true"/>
        <label text="{outdoor_hum}" font="16" color="#AAAAAA" align="center"/>
      </card>
    </row>
  </screen>

  <screen id="settings" bg="#1A1A2E">
    <settings_form/>
  </screen>

</screens>
```

---

## Firmware Implementation Tasks

1. **XML Parser** — parse layout.xml, build LVGL widget tree
   - Use a lightweight XML parser (TinyXML2 or custom)
   - Map XML elements to LVGL widget factory functions
   - Support nested elements (card > row > card > label)

2. **Placeholder Engine** — substitute {placeholders} at render time
   - Static placeholders: replaced once from data.json
   - Live placeholders: {time}, {indoor_temp} etc. use lv_timer to update labels

3. **Data Fetcher** — HTTP GET /layout.xml and /data.json
   - Cache layout.xml in NVS or SPIFFS (only refetch if server says changed)
   - Fetch data.json every 60 seconds
   - Pull-to-refresh: swipe down gesture triggers immediate refetch

4. **Swipe Navigation** — horizontal swipe between screens
   - lv_scr_load_anim with LV_SCR_LOAD_ANIM_MOVE_LEFT/RIGHT
   - Touch gesture detection

5. **Pull-to-Refresh** — swipe down gesture on any screen
   - Show spinner animation
   - Fetch data.json
   - Re-render placeholders

6. **Grove Sensor** — SHT40 or DHT20 on I2C
   - Read every 5 seconds via lv_timer
   - Update {indoor_temp} and {indoor_hum} labels directly

7. **Alert Banner** — shown only when data.json "alert" field is non-empty
   - Red card at bottom of home screen
   - visible attribute controlled by placeholder

8. **Settings Page** — hardcoded (not from XML)
   - WiFi SSID/password
   - Server IP + port
   - Timezone offset
   - Save to NVS, reboot

---

## Hardware Pins (SenseCAP Indicator D1 Pro)
- Display: ST7701S 480x480 RGB panel
- SPI init: MOSI=GPIO48, SCK=GPIO41
- CS/RST: PCA9535 I2C expander (addr 0x20), CS=P04, RST=P05
- I2C: SDA=GPIO39, SCL=GPIO40
- RGB: R=GPIO0-4, G=GPIO5-10, B=GPIO11-15
- HSYNC=GPIO16, VSYNC=GPIO17, DE=GPIO18, PCLK=GPIO21
- Backlight: GPIO45
- Touch: FT5X06 on I2C (addr 0x38)
- Grove I2C: same I2C bus (SDA=GPIO39, SCL=GPIO40)

## Current State
- Display working (ST7701S RGB panel driver via Arduino_GFX)
- Branch: feature/dashboard-v2
- LVGL 8.4.0
- PlatformIO + Arduino framework

---

## Prompt for Claude Code

```
Implement the Dashboard v2 for SenseCAP Indicator D1 Pro as described in SPEC_DASHBOARD_V2.md.

Start from scratch on the UI (keep display init, WiFi, NVS settings from current code).
Implement:
1. XML layout parser (TinyXML2 or lightweight alternative)
2. Placeholder engine with live updates for {time}, {indoor_temp}, {indoor_hum}
3. HTTP fetch for /layout.xml and /data.json from server
4. 4-page swipe navigation (Settings, Home, Calendar, Clock)
5. Pull-to-refresh on swipe down
6. Grove SHT40 sensor reading every 5s
7. Alert banner on home when alert non-empty

Build with: python3 -m platformio run
Fix all errors until SUCCESS.
git add -A && git commit -m "Implement dashboard v2 with XML layout engine" && git push origin feature/dashboard-v2
```
