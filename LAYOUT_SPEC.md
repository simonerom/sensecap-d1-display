# SenseCAP Dashboard Layout Specification
**Version: 1.0.0**
**Status: Draft**

This document is the contract between the OpenClaw server and the SenseCAP firmware.
Both sides MUST comply with this spec. Any change requires a version bump.

---

## Versioning

The server MUST include a `spec_version` field in every response.
The firmware MUST reject layouts with an incompatible major version.

Format: `MAJOR.MINOR.PATCH`
- **MAJOR**: breaking change (firmware update required)
- **MINOR**: new elements/placeholders added (backward compatible)
- **PATCH**: documentation or description fix only

---

## Endpoints

| Endpoint | Method | Content-Type | Description |
|----------|--------|--------------|-------------|
| `/layout.xml` | GET | `application/xml` | Page structure and style |
| `/data.json` | GET | `application/json` | Live values for placeholders |
| `/health` | GET | `application/json` | Server health check |

### Headers (server MUST include):
```
X-Layout-Version: 1.0.0
X-Data-Timestamp: 2026-03-02T14:00:00+01:00
Cache-Control: no-cache
```

---


### LVGL recolor syntax

### Friendly mini-syntax (server-side / runtime normalized)

The firmware also accepts a friendlier syntax in dynamic text values (placeholder data):

- `**text**` → emphasized (mapped to bright recolor)
- `_text_` → soft emphasis (mapped to accent recolor)
- `{#RRGGBB}text{/}` → explicit color span

These are normalized to LVGL recolor tags before rendering.


When `recolor="true"` (or `rich="true"`) is set on `<label>` / `<list>`, text can include:

```
#RRGGBB your text here#
```

Example:

```xml
<label text="#FF6B6B Allerta# #CFE8FF meteo in aggiornamento#" recolor="true"/>
```

## data.json

### Schema

```json
{
  "_version": "1.0.0",
  "_timestamp": "2026-03-02T14:00:00+01:00",

  "outdoor_temp": "12°C",
  "outdoor_hum": "78%",
  "condition": "Nuvoloso",
  "condition_icon": "cloudy",

  "btc_symbol": "BTC",
  "btc_price": "$66.298",
  "btc_change": "-0.3%",
  "btc_trend": "up",

  "eth_symbol": "ETH",
  "eth_price": "$1.948",
  "eth_change": "-1.8%",
  "eth_trend": "down",

  "iotx_symbol": "IOTX",
  "iotx_price": "$0.0046",
  "iotx_change": "+5.4%",
  "iotx_trend": "up",

  "news": [
    "Titolo notizia 1",
    "Titolo notizia 2"
  ],

  "events": [
    { "date": "2 mar", "time": "09:00", "title": "Riunione team" },
    { "date": "9 mar", "time": "08:30", "title": "Ecografia Fertilab" }
  ],

  "message": "Testo del messaggio di buongiorno...",
  "curiosity": "Curiosità del giorno...",
  "alert": ""
}
```

### Field Rules

| Field | Type | Required | Max length | Notes |
|-------|------|----------|------------|-------|
| `_version` | string | ✅ | — | Must match spec version |
| `_timestamp` | string | ✅ | — | ISO 8601 |
| `outdoor_temp` | string | ✅ | 8 chars | e.g. "12°C" |
| `outdoor_hum` | string | ✅ | 5 chars | e.g. "78%" |
| `condition` | string | ✅ | 32 chars | Human readable |
| `condition_icon` | string | ✅ | 32 chars | See icon codes below |
| `btc_price` | string | ✅ | 12 chars | e.g. "$66.298" |
| `btc_change` | string | ✅ | 8 chars | e.g. "-0.3%" |
| `btc_trend` | string | ✅ | 4 chars | `"up"` or `"down"` |
| `news` | array | ✅ | max 10 items | Each item max 120 chars |
| `events` | array | ✅ | max 20 items | See event object below |
| `events[].date` | string | ✅ | 12 chars | e.g. "2 mar" |
| `events[].time` | string | ✅ | 8 chars | e.g. "09:00" |
| `events[].title` | string | ✅ | 80 chars | — |
| `message` | string | ✅ | 2000 chars | Supports `\n` for newlines |
| `curiosity` | string | ✅ | 500 chars | — |
| `alert` | string | ✅ | 200 chars | Empty string = no alert |

### Condition Icon Codes

| Code | Meaning | Emoji |
|------|---------|-------|
| `sunny` | Soleggiato | ☀️ |
| `partly_cloudy` | Parzialmente nuvoloso | ⛅ |
| `cloudy` | Nuvoloso | ☁️ |
| `overcast` | Coperto | 🌥️ |
| `rain` | Pioggia | 🌧️ |
| `drizzle` | Pioggerella | 🌦️ |
| `snow` | Neve | ❄️ |
| `thunder` | Temporale | ⛈️ |
| `fog` | Nebbia | 🌫️ |
| `wind` | Vento | 💨 |
| `night_clear` | Notte serena | 🌙 |
| `night_cloudy` | Notte nuvolosa | 🌑 |

---

## layout.xml

### Root structure

```xml
<?xml version="1.0" encoding="UTF-8"?>
<screens version="1.0.0">
  <screen id="home" ... />
  <screen id="calendar" ... />
  <screen id="clock" ... />
  <screen id="settings" ... />
</screens>
```

### `<screens>` attributes

| Attribute | Type | Required | Description |
|-----------|------|----------|-------------|
| `version` | string | ✅ | Spec version |

### `<screen>` attributes

| Attribute | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `id` | string | ✅ | — | `home`, `calendar`, `clock`, `settings` |
| `bg` | color | ❌ | `#1A1A2E` | Background color |
| `pad` | int | ❌ | `8` | Padding in px |

### Navigation order (fixed)

```
[settings] ← swipe left ← [home] → swipe right → [calendar] → swipe right → [clock]
```

---

## Elements

### `<card>`

A rounded rectangle container. Can contain any other elements.

| Attribute | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `bg` | color | ❌ | `#16213E` | Background color |
| `radius` | int | ❌ | `16` | Corner radius in px |
| `pad` | int | ❌ | `16` | Inner padding in px |
| `w` | string | ❌ | `100%` | Width: `100%` or `auto` |
| `h` | string | ❌ | `auto` | Height: `100%`, `auto`, or px value |
| `scroll` | bool | ❌ | `false` | Enable vertical scroll |
| `visible` | placeholder | ❌ | `true` | Show/hide: `true`, `false`, or `{placeholder}` |
| `gap` | int | ❌ | `8` | Gap between children |

---

### `<row>`

Horizontal flex container. Children are laid out left to right.

| Attribute | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `gap` | int | ❌ | `8` | Gap between children in px |
| `pad` | int | ❌ | `0` | Outer padding in px |
| `align` | string | ❌ | `stretch` | `stretch`, `center`, `top`, `bottom` |

Children of `<row>` can use `flex="1"` to share available width equally.

---

### `<label>`

A text label.

| Attribute | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `text` | string | ✅ | — | Text content or `{placeholder}` |
| `font` | int | ❌ | `16` | Font size in pt |
| `color` | color | ❌ | `#FFFFFF` | Text color |
| `align` | string | ❌ | `left` | `left`, `center`, `right` |
| `bold` | bool | ❌ | `false` | Bold font weight |
| `italic` | bool | ❌ | `false` | Italic-like emphasis (underline fallback on current firmware fonts) |
| `recolor` | bool | ❌ | `false` | Enable LVGL inline color tags `#RRGGBB text#` |
| `rich` | bool | ❌ | `false` | Alias of `recolor` for readability |
| `max_lines` | int | ❌ | `0` | Max lines (0 = unlimited) |

---

### `<list>`

A vertical list of text items from a JSON array placeholder.

| Attribute | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `items` | placeholder | ✅ | — | Must be a `{placeholder}` resolving to array of strings |
| `font` | int | ❌ | `14` | Font size |
| `color` | color | ❌ | `#CCCCCC` | Text color |
| `divider` | color | ❌ | none | Divider line color between items |
| `max_lines` | int | ❌ | `2` | Max lines per item |
| `bullet` | string | ❌ | `•` | Bullet character |
| `bold` | bool | ❌ | `false` | Use bold font for each item |
| `italic` | bool | ❌ | `false` | Italic-like emphasis (underline fallback) |
| `recolor` | bool | ❌ | `false` | Enable LVGL inline color tags for each item |
| `rich` | bool | ❌ | `false` | Alias of `recolor` for readability |

---

### `<crypto_row>`

A single row showing a crypto asset: symbol + price + colored change.

| Attribute | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `symbol` | string/placeholder | ✅ | — | e.g. `"BTC"` or `{btc_symbol}` |
| `price` | string/placeholder | ✅ | — | e.g. `"$66.298"` or `{btc_price}` |
| `change` | string/placeholder | ✅ | — | e.g. `"-0.3%"` or `{btc_change}` |
| `trend` | string/placeholder | ✅ | — | `"up"` or `"down"` or `{btc_trend}` |
| `up_color` | color | ❌ | `#00D4AA` | Color when trend=up |
| `down_color` | color | ❌ | `#FF6B6B` | Color when trend=down |
| `symbol_color` | color | ❌ | inherited | Symbol text color |
| `price_color` | color | ❌ | inherited | Price text color |

---

### `<events_list>`

A list of calendar events with date, time and title.

| Attribute | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `items` | placeholder | ✅ | — | `{placeholder}` resolving to array of event objects |
| `font` | int | ❌ | `15` | Font size |
| `color` | color | ❌ | `#FFFFFF` | Title color |
| `date_color` | color | ❌ | `#00D4AA` | Date/time color |

---

### `<calendar_grid>`

A monthly calendar view with today highlighted.

| Attribute | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `year` | int/placeholder | ✅ | — | e.g. `2026` or `{cal_year}` |
| `month` | int/placeholder | ✅ | — | 1–12 or `{cal_month}` |
| `today` | int/placeholder | ✅ | — | 1–31 or `{cal_today}` |
| `highlight_color` | color | ❌ | `#00D4AA` | Today highlight color |
| `text_color` | color | ❌ | `#FFFFFF` | Day number color |
| `header_color` | color | ❌ | `#AAAAAA` | Mon/Tue/... header color |

---

### `<big_clock>`

A large live clock that updates every second locally (no server fetch needed).

| Attribute | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `font` | int | ❌ | `96` | Font size |
| `color` | color | ❌ | `#FFFFFF` | Text color |
| `align` | string | ❌ | `center` | `left`, `center`, `right` |
| `format` | string | ❌ | `HH:MM` | `HH:MM` or `HH:MM:SS` |

---

### `<settings_form>`

Built-in settings UI. Not configurable via XML (always the same).
Contains: WiFi SSID, WiFi password, Server IP, Server port, Timezone offset.

No attributes.

---

## Placeholders

### Syntax

- Single value: `{placeholder_name}`
- In text: `"Temp: {indoor_temp}"` → `"Temp: 22°C"`
- Boolean visibility: `visible="{alert_visible}"` where server sets `"true"` or `"false"`

### Live placeholders (updated by firmware locally)

| Placeholder | Type | Update rate | Notes |
|-------------|------|-------------|-------|
| `{time}` | string | 1s | Current time HH:MM |
| `{time_seconds}` | string | 1s | Current time HH:MM:SS |
| `{date}` | string | 1min | e.g. "2 marzo 2026" |
| `{day}` | string | 1min | Day number e.g. "2" |
| `{weekday}` | string | 1min | e.g. "Lunedì" |
| `{month}` | string | 1min | Full Italian name e.g. "Marzo" (NOT "mar") |
| `{cal_year}` | int | daily | Current year |
| `{cal_month}` | int | daily | Current month 1-12 |
| `{cal_today}` | int | daily | Current day 1-31 |
| `{indoor_temp}` | string | 5s | From Grove sensor |
| `{indoor_hum}` | string | 5s | From Grove sensor |

### Server placeholders (from data.json)

All fields in data.json are available as `{field_name}`.
Arrays (`news`, `events`) are only valid in `<list>` and `<events_list>` elements.

---

## Color Format

All color attributes use hex RGB format: `#RRGGBB`

Predefined theme colors (recommended):

| Name | Value | Usage |
|------|-------|-------|
| `#1A1A2E` | Deep navy | Screen background |
| `#16213E` | Dark blue | Card background |
| `#00D4AA` | Teal | Primary accent, indoor |
| `#74B9FF` | Sky blue | Secondary accent, outdoor |
| `#FF6B6B` | Coral red | Alerts, negative |
| `#FFB347` | Orange | Warnings |
| `#FFFFFF` | White | Primary text |
| `#AAAAAA` | Gray | Secondary text |
| `#CCCCCC` | Light gray | Body text |

---

## Firmware Behavior

### Boot sequence
1. Load saved WiFi + server settings from NVS
2. If no settings → show `settings` screen directly
3. Connect to WiFi → show "Connessione in corso..."
4. Fetch `/layout.xml` → parse and build UI
5. Fetch `/data.json` → populate placeholders
6. Show `home` screen
7. Start timers: data refresh every 60s, live placeholders continuously

### Pull-to-refresh
- Gesture: swipe down from top of any screen
- Action: show spinner, fetch `/data.json`, update placeholders
- Timeout: 10 seconds max, show error toast on failure

### Error handling
- WiFi disconnected: show toast "WiFi disconnesso", retry every 30s
- Server unreachable: show last cached data with "Dati non aggiornati" badge
- Invalid JSON/XML: keep previous data, log error

### Caching
- `/layout.xml`: cached in SPIFFS, refetched only when `X-Layout-Version` header changes
- `/data.json`: never cached, always fresh

---

## Changelog

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2026-03-02 | Initial spec |

