# SenseCAP Indicator D1 - Display Firmware

Firmware PlatformIO per il **Seeed SenseCAP Indicator D1** (ESP32-S3 + display touchscreen da 4 pollici).

Il dispositivo si connette al WiFi, effettua polling ogni 60 secondi di un endpoint HTTP locale e mostra i dati su un'interfaccia LVGL con 3 pagine scorrevoli.

---

## Funzionalita'

- **Connessione WiFi** automatica con riconnessione in caso di perdita del segnale
- **Polling HTTP** ogni 60 secondi di un endpoint locale configurabile
- **UI LVGL** con 3 pagine swipe orizzontale:
  - Pagina 1: Data e messaggio
  - Pagina 2: Condizioni meteo
  - Pagina 3: Alert/notifiche
- **Tema scuro** con font grandi e indicatori di navigazione (dots)
- **Architettura multi-task** FreeRTOS (UI su core 1, rete su core 0)
- **Aggiornamento automatico** ogni 60 secondi

---

## Struttura del progetto

```
sensecap-d1-display/
├── platformio.ini          # Configurazione PlatformIO
├── include/
│   ├── config.h            # Configurazione WiFi, endpoint, colori
│   └── lv_conf.h           # Configurazione LVGL
├── src/
│   ├── main.cpp            # Entry point e task FreeRTOS
│   ├── wifi_manager.h/.cpp # Gestione connessione WiFi
│   ├── data_fetcher.h/.cpp # Fetch e parsing dati HTTP/JSON
│   └── ui.h/.cpp           # UI LVGL (3 pagine + overlay)
├── scripts/
│   └── setup_lvgl.py       # Script pre-build per configurare LVGL
└── README.md
```

---

## Configurazione

Modifica il file `include/config.h` prima di compilare:

```c
// WiFi
#define WIFI_SSID       "TuaReteWiFi"
#define WIFI_PASSWORD   "TuaPassword"

// Endpoint HTTP locale
#define DATA_ENDPOINT_HOST  "192.168.1.100"
#define DATA_ENDPOINT_PORT  8080
#define DATA_ENDPOINT_PATH  "/api/display"

// Intervallo di polling (millisecondi)
#define POLL_INTERVAL_MS    60000
```

---

## Formato risposta API

L'endpoint deve restituire un oggetto JSON con questo schema:

```json
{
  "date":    "Domenica 1 Marzo 2026 - 10:30",
  "message": "Buongiorno! Hai 3 nuovi messaggi.",
  "weather": "Soleggiato, 18°C - Vento 10 km/h",
  "alert":   "Riunione alle 15:00 in sala conferenze"
}
```

| Campo     | Tipo   | Descrizione                                      |
|-----------|--------|--------------------------------------------------|
| `date`    | string | Data e ora da mostrare nella pagina 1            |
| `message` | string | Messaggio principale della pagina 1              |
| `weather` | string | Condizioni meteo per la pagina 2                 |
| `alert`   | string | Testo alert per la pagina 3 (`""` = nessun alert)|

---

## Dipendenze

| Libreria       | Versione | Descrizione                    |
|----------------|----------|--------------------------------|
| `lvgl/lvgl`    | ^8.3.11  | GUI library                    |
| `bodmer/TFT_eSPI` | ^2.5.43 | Driver display SPI          |
| `bblanchon/ArduinoJson` | ^7.0.4 | Parsing JSON             |

---

## Build e flash

### Prerequisiti

- [PlatformIO](https://platformio.org/) (estensione VS Code o CLI)
- Python 3.x (per lo script pre-build)

### Compilazione

```bash
# Da riga di comando
pio run

# Flash sul dispositivo
pio run --target upload

# Monitor seriale
pio device monitor
```

### Prima compilazione

1. Clona il repository
2. Modifica `include/config.h` con le credenziali WiFi e l'endpoint
3. Connetti il SenseCAP Indicator D1 via USB
4. Esegui `pio run --target upload`

---

## Hardware: SenseCAP Indicator D1

| Componente      | Specifiche                        |
|-----------------|-----------------------------------|
| MCU principale  | ESP32-S3 (240 MHz, WiFi + BT)    |
| MCU secondario  | RP2040 (non usato in questo fw)  |
| Display         | 4" IPS touchscreen 480x320       |
| Driver display  | ILI9341 (SPI)                    |
| Touch           | Resistivo (calibrazione inclusa) |
| Flash           | 8 MB                             |
| PSRAM           | 8 MB (OSPI)                      |

---

## Pin display (ESP32-S3)

| Funzione | GPIO |
|----------|------|
| MOSI     | 11   |
| SCLK     | 12   |
| CS       | 10   |
| DC       | 14   |
| RST      | 9    |
| Backlight| 45   |
| Touch CS | 8    |

---

## Architettura firmware

```
setup()
 ├── lvgl_display_init()   # Init TFT_eSPI + driver LVGL
 ├── lvgl_tick_timer_init() # Timer ISR 5ms per LVGL
 ├── ui.init()             # Crea le 3 pagine LVGL
 ├── taskUI (Core 1)       # Loop LVGL ogni 5ms
 └── taskNetwork (Core 0)  # WiFi + fetch ogni 60s
```

### Flusso dati

```
taskNetwork:
  wifiMgr.ensureConnected()
    └─> WiFi.begin() / reconnect
  fetcher.fetch(data)
    └─> HTTP GET -> JSON parse -> DisplayData
  ui.updateData(data)
    └─> aggiorna labels LVGL

taskUI:
  ui.tick()
    └─> lv_timer_handler()  # Ridisegna se necessario
```

---

## Personalizzazione UI

I colori sono definiti in `include/config.h`:

```c
#define COLOR_BG        0x1A1A2E  // Sfondo principale
#define COLOR_PAGE1     0x16213E  // Sfondo pagina 1
#define COLOR_ACCENT    0x00D4AA  // Colore accento (verde-acqua)
#define COLOR_ALERT     0xFF6B6B  // Colore alert (rosso)
#define COLOR_WEATHER   0x74B9FF  // Colore meteo (azzurro)
```

---

## Server di esempio (Python)

Un semplice server Flask per testare il firmware:

```python
from flask import Flask, jsonify
from datetime import datetime

app = Flask(__name__)

@app.route('/api/display')
def display_data():
    return jsonify({
        "date":    datetime.now().strftime("%A %d %B %Y - %H:%M"),
        "message": "Sistema operativo. Tutto nella norma.",
        "weather": "Soleggiato, 18°C - Umidita' 65%",
        "alert":   ""  # Stringa vuota = nessun alert
    })

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=8080)
```

Avvia con: `python server.py`

---

## Calibrazione touch

I valori di calibrazione del touch resistivo si trovano in `src/ui.cpp`:

```cpp
uint16_t calData[5] = {275, 3620, 264, 3532, 1};
tft.setTouchCalibrate(calData);
```

Per ricalibrare, esegui lo sketch di calibrazione di TFT_eSPI e sostituisci i valori.

---

## Licenza

MIT License - vedi [LICENSE](LICENSE) per i dettagli.
