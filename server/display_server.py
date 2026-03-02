#!/usr/bin/env python3
"""
SenseCAP D1 Pro — Display Server
Serves /layout.xml, /data.json, /health for the device.
Spec version: 1.0.0
"""

import json
import re
import subprocess
import threading
import urllib.request
from datetime import datetime, timedelta
from http.server import BaseHTTPRequestHandler, HTTPServer

import caldav
import pytz


# Emoji → monochrome Unicode substitution map
EMOJI_MONO = {
    "☀️": "☀", "🌤️": "☁", "⛅": "☁", "🌥️": "☁", "🌦️": "☁",
    "🌧️": "☁", "🌨️": "☁", "🌩️": "⚡", "⛈️": "⚡", "🌪️": "~",
    "🌡️": "▲", "💧": "~", "❄️": "*", "🌈": "~",
    "📰": "★",
    # Curly quotes → straight (not in font)
    "‘": "'", "’": "'",   # ' '
    "“": '"', "”": '"',   # " "
    "–": "-", "—": "-",   # – —
    "…": "...",                # …
    "«": '"', "»": '"',   # « » "📅": "◉", "📆": "◉", "🗓️": "◉",
    "💰": "$", "💵": "$", "💶": "$", "📈": "▲", "📉": "▼",
    "⭐": "★", "🌟": "★", "✨": "★", "💡": "★",
    "⚠️": "⚠", "❗": "!", "❕": "!", "🔴": "(!)",
    "🌍": "◆", "🌎": "◆", "🌏": "◆", "🗺️": "◆",
    "👋": "", "🙌": "", "👍": "", "🎉": "",
    "🚇": "▶", "🚌": "▶", "🚗": "▶", "✈️": "»",
    "🧠": "", "🤔": "",
}

def strip_emoji(text):
    """Replace known emoji with monochrome symbols, drop the rest."""
    import unicodedata
    # Apply substitution map first
    for emoji, mono in EMOJI_MONO.items():
        text = text.replace(emoji, mono)
    result = []
    for ch in text:
        cat = unicodedata.category(ch)
        cp  = ord(ch)
        # Keep: ASCII printable, accented latin (U+00C0–U+024F), common punctuation
        if (0x20 <= cp <= 0x7E) or (0x00C0 <= cp <= 0x024F) or (0x2018 <= cp <= 0x201F) or cp == 0xB0:
            result.append(ch)
        elif ch == "\n":  # preserve newlines
            result.append("\n")
        elif cat in ("Zs",):  # spaces
            result.append(" ")
        # else: drop (emoji, symbols, CJK, etc.)
    # Collapse multiple spaces
    import re as _re
    return _re.sub(r" {2,}", " ", "".join(result)).strip()

# ─── Config ───────────────────────────────────────────────────────────────────
PORT = 8765
SPEC_VERSION = "1.3.4"
TZ = pytz.timezone("Europe/Rome")
CALDAV_USER = "mail@sromano.com"

DAYS_IT   = ["Lunedì","Martedì","Mercoledì","Giovedì","Venerdì","Sabato","Domenica"]

# ─── Background cache ─────────────────────────────────────────────────────────
_cache_lock = threading.Lock()
_cached_data = None
_cache_ts    = 0
CACHE_TTL    = 60  # seconds

def _refresh_cache():
    global _cached_data, _cache_ts
    try:
        data = build_data()
        data["alert_visible"] = "true" if data["alert"] else "false"
        with _cache_lock:
            _cached_data = data
            _cache_ts    = datetime.now(TZ).timestamp()
    except Exception as e:
        print(f"[cache] refresh error: {e}")

def _schedule_refresh():
    t = threading.Thread(target=_refresh_loop, daemon=True)
    t.start()

def _refresh_loop():
    import time
    while True:
        _refresh_cache()
        time.sleep(CACHE_TTL)

def get_cached_data():
    with _cache_lock:
        return _cached_data
MONTHS_IT = ["Gennaio","Febbraio","Marzo","Aprile","Maggio","Giugno",
             "Luglio","Agosto","Settembre","Ottobre","Novembre","Dicembre"]


def is_italian_holiday(dt):
    """Return True if dt is a weekend or Italian public holiday."""
    if dt.weekday() == 6:  # Sun only
        return True
    d, m = dt.day, dt.month
    # Fixed holidays
    fixed = {(1,1),(6,1),(25,4),(1,5),(2,6),(15,8),(1,11),(8,12),(25,12),(26,12)}
    if (d, m) in fixed:
        return True
    # Easter (Gauss algorithm)
    y = dt.year
    a = y % 19
    b = y // 100
    c = y % 100
    d_ = b // 4
    e = b % 4
    f = (b + 8) // 25
    g = (b - f + 1) // 3
    h = (19*a + b - d_ - g + 15) % 30
    i = c // 4
    k = c % 4
    l = (32 + 2*e + 2*i - h - k) % 7
    m_ = (a + 11*h + 22*l) // 451
    month = (h + l - 7*m_ + 114) // 31
    day   = ((h + l - 7*m_ + 114) % 31) + 1
    from datetime import date
    easter = date(y, month, day)
    pasquetta = date(y, month, day) + timedelta(days=1)
    if dt.date() in (easter, pasquetta.date() if hasattr(pasquetta,'date') else pasquetta):
        return True
    return False

CONDITION_ICONS = {
    "sunny": "sunny", "clear": "sunny", "partly": "partly_cloudy",
    "cloud": "cloudy", "overcast": "overcast", "rain": "rain",
    "drizzle": "drizzle", "snow": "snow", "thunder": "thunder",
    "fog": "fog", "mist": "fog", "haze": "fog", "wind": "wind",
}

# ─── Helpers ──────────────────────────────────────────────────────────────────

def caldav_password():
    r = subprocess.run(
        ["security","find-generic-password","-a",CALDAV_USER,
         "-s","icloud-caldav-openclaw","-w"],
        capture_output=True, text=True)
    return r.stdout.strip()

def get_weather():
    try:
        url = "https://api.open-meteo.com/v1/forecast?latitude=45.4654&longitude=9.1859" \
              "&current=temperature_2m,relative_humidity_2m,weathercode,windspeed_10m" \
              "&daily=weathercode,temperature_2m_max,temperature_2m_min,precipitation_sum" \
              "&hourly=temperature_2m,precipitation_probability,weathercode" \
              "&timezone=Europe%2FRome&forecast_days=3"
        with urllib.request.urlopen(url, timeout=8) as r:
            d = json.loads(r.read())

        cur = d["current"]
        temp = round(cur["temperature_2m"])
        hum  = cur["relative_humidity_2m"]
        code = cur["weathercode"]
        wind = round(cur["windspeed_10m"])

        # Map WMO code to condition string
        def wmo_to_desc(c):
            if c == 0: return "Soleggiato"
            elif c <= 3: return "Parzialmente nuvoloso"
            elif c <= 48: return "Nebbia"
            elif c <= 57: return "Pioggerella"
            elif c <= 67: return "Pioggia"
            elif c <= 77: return "Neve"
            elif c <= 82: return "Rovesci"
            elif c <= 99: return "Temporale"
            return "N/D"

        def wmo_to_icon(c):
            if c == 0: return "sunny"
            elif c <= 2: return "partly_cloudy"
            elif c <= 3: return "cloudy"
            elif c <= 48: return "fog"
            elif c <= 67: return "rain"
            elif c <= 77: return "snow"
            elif c <= 82: return "drizzle"
            elif c <= 99: return "thunder"
            return "cloudy"

        condition = wmo_to_desc(code)
        icon      = wmo_to_icon(code)

        # Hourly rain probability for Roberta's commute windows
        hours = d["hourly"]["time"]
        prec  = d["hourly"]["precipitation_probability"]
        now   = datetime.now(TZ)

        def rain_prob_window(h_start, h_end):
            probs = []
            for i, t in enumerate(hours):
                dt = datetime.fromisoformat(t)
                if dt.date() == now.date() and h_start <= dt.hour < h_end:
                    probs.append(prec[i])
            return max(probs) if probs else 0

        rain_morning = rain_prob_window(8, 9)    # 8:30-9:00 commute
        rain_evening = rain_prob_window(17, 18)  # 17:30-18:00 commute

        # 3-day forecast
        forecast = []
        for i in range(min(3, len(d["daily"]["time"]))):
            forecast.append({
                "date": d["daily"]["time"][i],
                "desc": wmo_to_desc(d["daily"]["weathercode"][i]),
                "max":  round(d["daily"]["temperature_2m_max"][i]),
                "min":  round(d["daily"]["temperature_2m_min"][i]),
                "precip": d["daily"]["precipitation_sum"][i],
            })

        return {
            "outdoor_temp": f"{temp}°C",
            "outdoor_hum":  f"{hum}%",
            "condition":    condition,
            "condition_icon": icon,
            "wind":         f"{wind} km/h",
            "rain_morning": rain_morning,
            "rain_evening": rain_evening,
            "forecast":     forecast,
        }
    except Exception as e:
        return {
            "outdoor_temp": "--",
            "outdoor_hum":  "--",
            "condition":    "N/D",
            "condition_icon": "cloudy",
            "wind":         "--",
            "rain_morning": 0,
            "rain_evening": 0,
            "forecast":     [],
        }

def get_crypto():
    try:
        url = "https://api.coingecko.com/api/v3/simple/price" \
              "?ids=bitcoin,ethereum,iotex&vs_currencies=usd&include_24hr_change=true"
        with urllib.request.urlopen(url, timeout=8) as r:
            d = json.loads(r.read())
        def fmt(coin, sym):
            p   = d[coin]["usd"]
            chg = d[coin].get("usd_24h_change", 0)
            trend = "up" if chg >= 0 else "down"
            price = f"${p:,.0f}" if p > 100 else f"${p:.4f}"
            return {"symbol": sym, "price": price,
                    "change": f"{chg:+.1f}%", "trend": trend}
        return {
            "btc": fmt("bitcoin","BTC"),
            "eth": fmt("ethereum","ETH"),
            "iotx": fmt("iotex","IOTX"),
        }
    except:
        empty = {"symbol":"--","price":"--","change":"--","trend":"down"}
        return {"btc": empty, "eth": empty, "iotx": empty}

def _parse_rss_titles(raw, skip=1, limit=4):
    """Extract titles from RSS feed (handles CDATA and plain)."""
    import re as _re
    titles = _re.findall(r'<title><!\[CDATA\[(.*?)\]\]></title>', raw, _re.DOTALL)
    if not titles:
        titles = _re.findall(r'<title>(.*?)</title>', raw, _re.DOTALL)
    titles = [_re.sub(r'<!\[CDATA\[|\]\]>', '', t).strip() for t in titles]
    titles = [strip_emoji(t) for t in titles if t and len(t) > 10]
    titles = [t for t in titles if len(t) > 10]
    return titles[skip:skip + limit]

def _get_scioperi_summary():
    """Parse MIT scioperi registry for upcoming ATM/metro Milan strikes and others."""
    try:
        req = urllib.request.Request(
            "https://scioperi.mit.gov.it/mit2/public/scioperi",
            headers={"User-Agent": "Mozilla/5.0", "Accept-Encoding": "identity"})
        with urllib.request.urlopen(req, timeout=10) as r:
            raw = r.read().decode("utf-8", errors="replace")

        # Strip tags and split into rows by date pattern DD/MM/YYYY
        text = re.sub(r"<[^>]+>", " ", raw)
        text = re.sub(r"\s+", " ", text)

        from datetime import date as _date
        today = _date.today()
        MONTHS_IT = ["","gennaio","febbraio","marzo","aprile","maggio","giugno",
                     "luglio","agosto","settembre","ottobre","novembre","dicembre"]

        # Find rows: date + sector + who + duration + scope
        pattern = r"(\d{2}/\d{2}/\d{4})\s+\1\s+[^\d]{5,80?}?(Trasporto pubblico locale|Generale|Ferroviario|Aereo)[^\d]{0,200?}?(\d{1,2} ORE|INTERA GIORNATA|ORE \d|\d ORE)"
        entries = re.findall(
            r"(\d{2}/\d{2}/\d{4})\s+\1\s+([\w\s\-/]+?)\s+(Trasporto pubblico locale|Generale|Ferroviario|Aereo)\s+([\w\s\(\)\.,:]+?)\s+(\d+ ORE|INTERA GIORNATA|[\d\.]+ ORE: [^\s]+)",
            text)

        hits = []
        seen = set()
        for m in entries:
            date_str, union, sector, who, duration = m
            try:
                d, mo, y = int(date_str[:2]), int(date_str[3:5]), int(date_str[6:])
                ev_date = _date(y, mo, d)
            except: continue
            if ev_date <= today: continue

            date_it = f"{d} {MONTHS_IT[mo]}"
            who_up = who.upper()

            is_atm_milan = "ATM" in who_up and ("MILANO" in who_up or "MILAN" in who_up or sector == "Trasporto pubblico locale")
            is_general = sector == "Generale" and "ESCLUSIONE" not in who_up and "ESCLUSO" not in who_up

            if is_atm_milan:
                dur = duration.strip()
                if "INTERA" in dur or "24" in dur: dur_str = "24h"
                else:
                    m2 = re.match(r"(\d+) ORE", dur)
                    dur_str = f"{m2.group(1)}h" if m2 else dur[:10]
                line = f"{date_it}: ATM Milano - {dur_str}"
            elif is_general:
                line = f"{date_it}: sciopero generale"
            elif sector == "Ferroviario" and "NAZIONALE" in text[text.find(date_str):text.find(date_str)+200].upper():
                line = f"{date_it}: treni"
            elif sector == "Aereo":
                line = f"{date_it}: aerei"
            else:
                continue

            if line not in seen:
                seen.add(line)
                hits.append((ev_date, line))

        hits.sort(key=lambda x: x[0])
        return [h[1] for h in hits[:4]]
    except Exception as e:
        return []

def _fetch_rss(url, skip=1, limit=4):
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=7) as r:
            raw = r.read().decode("utf-8", errors="replace")
        return _parse_rss_titles(raw, skip=skip, limit=limit)
    except:
        return []

def get_news():
    italia  = _fetch_rss("https://www.ansa.it/sito/ansait_rss.xml", skip=1, limit=4)
    estero  = _fetch_rss("https://news.google.com/rss/search?q=mondo&hl=it&gl=IT&ceid=IT:it", skip=2, limit=4)
    milano  = _fetch_rss("https://news.google.com/rss/search?q=Milano&hl=it&gl=IT&ceid=IT:it", skip=2, limit=3)
    scioperi = _get_scioperi_summary()
    return {
        "italia":  [strip_emoji(t) for t in italia[:3]],
        "estero":  [strip_emoji(t) for t in estero[:3]],
        "milano":  [strip_emoji(t) for t in milano[:2]],
        "scioperi":[strip_emoji(t) for t in scioperi[:2]],
    }

def get_events():
    try:
        pwd = caldav_password()
        client = caldav.DAVClient(
            url="https://caldav.icloud.com",
            username=CALDAV_USER, password=pwd)
        cal = next(c for c in client.principal().calendars() if c.name == "iCloud")
        now   = datetime.now(TZ)
        start = now.replace(hour=0, minute=0, second=0, microsecond=0)
        end   = start + timedelta(days=7)
        evts  = cal.search(start=start, end=end, event=True, expand=True)
        result = []
        for e in evts:
            try:
                v    = e.vobject_instance.vevent
                title = str(v.summary.value)
                dtstart = v.dtstart.value
                if hasattr(dtstart, 'strftime'):
                    if hasattr(dtstart, 'tzinfo') and dtstart.tzinfo:
                        dtstart = dtstart.astimezone(TZ)
                    day   = dtstart.strftime("%-d %b").lower()
                    time_ = dtstart.strftime("%H:%M")
                else:
                    day   = dtstart.strftime("%-d %b").lower()
                    time_ = "tutto il giorno"
                result.append({"date": day, "time": time_, "title": title})
            except: pass
        return sorted(result, key=lambda x: x["date"])[:10]
    except:
        return []

def get_curiosity(now):
    curiosities = {
        (3, 2):  "Il 2 marzo 1969 volò per la prima volta il Concorde, l'aereo supersonico franco-britannico capace di collegare Parigi a New York in meno di 4 ore. Rimase in servizio fino al 2003. ✈️",
        (3, 3):  "Il 3 marzo 1847 nacque Alexander Graham Bell, inventore del telefono. La sua prima parola trasmessa fu: 'Mr. Watson, come here, I want to see you.' 📞",
        (3, 4):  "Il 4 marzo 1681 il re Carlo II d'Inghilterra firmò la Carta della Pennsylvania, affidando il territorio a William Penn. La Pennsylvania prende il nome dal suo fondatore. 🏛️",
        (3, 5):  "Il 5 marzo 1616 la Chiesa Cattolica dichiarò ufficialmente eretiche le teorie di Copernico sul sistema solare eliocentrico. Ci vollero altri 200 anni per riabilitarlo. 🌍",
    }
    key = (now.month, now.day)
    return curiosities.get(key, 
        "Lo sapevi? Il cervello umano elabora le immagini 60.000 volte più velocemente del testo. Ecco perché una bella UI vale più di mille parole.")

def build_roberta_note(now, rain_morning, rain_evening):
    """Return commute warning if today is Tue/Thu and rain is likely."""
    weekday = now.weekday()  # 0=Mon, 1=Tue, 3=Thu
    if weekday not in [1, 3]:
        return ""
    notes = []
    if rain_morning >= 40:
        notes.append(f"Andata 8:30–9:00: ~ Pioggia ({rain_morning}%) — porta l'ombrello!")
    else:
        notes.append(f"Andata 8:30–9:00: * Ok ({rain_morning}%)")
    end_time = "17:00–17:30" if weekday == 4 else "17:30–18:00"
    if rain_evening >= 40:
        notes.append(f"Ritorno {end_time}: ~ Pioggia ({rain_evening}%) — occhio!")
    else:
        notes.append(f"Ritorno {end_time}: * Ok ({rain_evening}%)")
    return "\n".join(notes)



def build_meteo_summary(now, weather, events):
    parts = []
    parts.append(f"{weather['condition']}, {weather['outdoor_temp']}, vento {weather['wind']}")
    roberta = build_roberta_note(now, weather["rain_morning"], weather["rain_evening"])
    if roberta:
        parts.append(f"\n▶ Roberta, se oggi vai in ufficio:\n{roberta}")
    today_str = now.strftime("%-d %b").lower()
    today_events = [e for e in events if e["date"] == today_str]
    if today_events:
        parts.append("\n◉ Oggi:")
        for e in today_events:
            parts.append(f"  {e['time']} - {e['title']}")
    return "\n".join(parts)

# ─── Data builder ─────────────────────────────────────────────────────────────

def build_data():
    now     = datetime.now(TZ)
    weather = get_weather()
    crypto  = get_crypto()
    news     = get_news()
    events   = get_events()
    meteo_summary = build_meteo_summary(now, weather, events)
    curiosity = get_curiosity(now)

    return {
        "_version":   SPEC_VERSION,
        "_timestamp": now.isoformat(),

        "outdoor_temp":    weather["outdoor_temp"],
        "outdoor_hum":     weather["outdoor_hum"],
        "condition":       weather["condition"],
        "condition_icon":  weather["condition_icon"],

        "btc_symbol": crypto["btc"]["symbol"],
        "btc_price":  crypto["btc"]["price"],
        "btc_change": crypto["btc"]["change"],
        "btc_trend":  crypto["btc"]["trend"],

        "eth_symbol": crypto["eth"]["symbol"],
        "eth_price":  crypto["eth"]["price"],
        "eth_change": crypto["eth"]["change"],
        "eth_trend":  crypto["eth"]["trend"],

        "iotx_symbol": crypto["iotx"]["symbol"],
        "iotx_price":  crypto["iotx"]["price"],
        "iotx_change": crypto["iotx"]["change"],
        "iotx_trend":  crypto["iotx"]["trend"],

        "news_italia":  news["italia"],
        "news_estero":  news["estero"],
        "news_milano":  news["milano"],
        "scioperi":     news["scioperi"],
        "scioperi_text": "\n".join(news["scioperi"]) if news["scioperi"] else "",
        "scioperi_visible": "true" if news["scioperi"] else "false",
        "events":    events,
        "meteo_summary": strip_emoji(meteo_summary),
        "curiosity": strip_emoji(curiosity),
        "month_name": MONTHS_IT[now.month - 1],
        "voc":       "--",
        "co2":       "--",
        "alert":     "",
        "day_color":  "#E53935" if is_italian_holiday(now) else "#1A1A2E",
    }

# ─── Layout XML (light theme) ─────────────────────────────────────────────────

LAYOUT_XML = """<?xml version="1.0" encoding="UTF-8"?>
<screens version="1.3.4">

  <screen id="home" bg="#C8F0E8">
    <row gap="10" pad="10" h="310">
      <!-- Left col: Interno + Esterno -->
      <col flex="1" gap="10">
        <card bg="#FFFFFF" radius="6" pad="16" flex="1">
          <label text="Interno" font="18" color="#1A1A2E" bold="true" align="center"/>
          <label text="{indoor_temp}" font="24" color="#00A885" align="center" bold="true"/>
          <label text="{indoor_hum}" font="16" color="#888888" align="center"/>
        </card>
        <card bg="#FFFFFF" radius="6" pad="16" flex="1">
          <label text="Esterno" font="18" color="#1A1A2E" bold="true" align="center"/>
          <label text="{outdoor_temp}" font="24" color="#2B7DE9" align="center" bold="true"/>
          <label text="{outdoor_hum}" font="16" color="#888888" align="center"/>
        </card>
      </col>
      <!-- Center: tall date card -->
      <card bg="#FFFFFF" radius="6" pad_h="8" pad_v="8" gap="4" tight="true" flex="2" h="100%" valign="center">
        <label text="{weekday}" font="28" color="#444444" align="center"/>
        <label text="{day}" font="192" color="{day_color}" align="center" bold="true"/>
        <label text="{month_name}" font="28" color="#444444" align="center"/>
      </card>
      <!-- Right col: VOC + spare -->
      <col flex="1" gap="10">
        <card bg="#FFFFFF" radius="6" pad="16" flex="1">
          <label text="VOC" font="18" color="#1A1A2E" bold="true" align="center"/>
          <label text="{voc}" font="24" color="#00A885" align="center" bold="true"/>
        </card>
        <card bg="#FFFFFF" radius="6" pad="16" flex="1">
          <label text="CO2" font="18" color="#1A1A2E" bold="true" align="center"/>
          <label text="{co2}" font="24" color="#2B7DE9" align="center" bold="true"/>
        </card>
      </col>
    </row>
    <!-- Bottom: scrollable full buongiorno -->
    <card bg="#FFFFFF" radius="6" pad="14" w="100%" scroll="true" gap="16">
      <card bg="#FFF3E0" radius="6" pad="10" w="100%" visible="{scioperi_visible}">
        <label text="⚠ Scioperi in arrivo" font="18" color="#E65100" bold="true"/>
        <label text="{scioperi_text}" font="16" color="#BF360C" max_lines="0"/>
      </card>
      <label text="☁ Meteo" font="22" color="#1A1A2E" bold="true"/>
      <label text="{meteo_summary}" font="18" color="#444444" max_lines="0"/>
      <label text="★ Notizie Italia" font="22" color="#1A1A2E" bold="true"/>
      <list items="{news_italia}" font="18" color="#333333" divider="#DDDDDD" max_lines="2"/>
      <label text="★ Notizie Estero" font="22" color="#1A1A2E" bold="true"/>
      <list items="{news_estero}" font="18" color="#333333" divider="#DDDDDD" max_lines="2"/>
      <label text="★ Milano" font="22" color="#1A1A2E" bold="true"/>
      <list items="{news_milano}" font="18" color="#333333" divider="#DDDDDD" max_lines="2"/>
      <label text="$ Mercati" font="22" color="#1A1A2E" bold="true"/>
      <crypto_row symbol="{btc_symbol}" price="{btc_price}" change="{btc_change}" trend="{btc_trend}" up_color="#00A885" down_color="#E53935"/>
      <crypto_row symbol="{eth_symbol}" price="{eth_price}" change="{eth_change}" trend="{eth_trend}" up_color="#00A885" down_color="#E53935"/>
      <crypto_row symbol="{iotx_symbol}" price="{iotx_price}" change="{iotx_change}" trend="{iotx_trend}" up_color="#00A885" down_color="#E53935"/>
      <label text="◆ Curiosita" font="22" color="#1A1A2E" bold="true"/>
      <label text="{curiosity}" font="18" color="#555555" max_lines="0"/>
    </card>
    <card bg="#E53935" radius="6" pad="12" w="100%" visible="{alert_visible}">
      <label text="⚠ {alert}" font="16" color="#FFFFFF" align="center"/>
    </card>
  </screen>

  <screen id="calendar" bg="#F5F5F5">
    <calendar_grid year="{cal_year}" month="{cal_month}" today="{cal_today}"
      highlight_color="#00A885" text_color="#1A1A2E" header_color="#888888"/>
    <card bg="#FFFFFF" radius="6" pad="16" w="100%" scroll="true">
      <label text="◉ Prossimi eventi" font="18" color="#1A1A2E" bold="true"/>
      <events_list items="{events}" font="15" color="#1A1A2E" date_color="#00A885"/>
    </card>
    <row gap="12" pad="12">
      <card flex="1" bg="#FFFFFF" radius="6" pad="12">
        <label text="▲ Interno" font="13" color="#666666" align="center"/>
        <label text="{indoor_temp}" font="24" color="#00A885" align="center" bold="true"/>
        <label text="{indoor_hum}" font="13" color="#888888" align="center"/>
      </card>
      <card flex="1" bg="#FFFFFF" radius="6" pad="12">
        <label text="☁ Esterno" font="13" color="#666666" align="center"/>
        <label text="{outdoor_temp}" font="24" color="#2B7DE9" align="center" bold="true"/>
        <label text="{outdoor_hum}" font="13" color="#888888" align="center"/>
      </card>
    </row>
  </screen>

  <screen id="clock" bg="#F5F5F5">
    <big_clock font="128" color="#1A1A2E" align="center" format="HH:MM:SS" bold="true"/>
    <label text="{weekday} {day} {month}" font="20" color="#666666" align="center"/>
    <row gap="12" pad="16">
      <card flex="1" bg="#FFFFFF" radius="6" pad="16">
        <label text="▲ Interno" font="13" color="#666666" align="center"/>
        <label text="{indoor_temp}" font="32" color="#00A885" align="center" bold="true"/>
        <label text="{indoor_hum}" font="16" color="#888888" align="center"/>
      </card>
      <card flex="1" bg="#FFFFFF" radius="6" pad="16">
        <label text="☁ Esterno" font="13" color="#666666" align="center"/>
        <label text="{outdoor_temp}" font="32" color="#2B7DE9" align="center" bold="true"/>
        <label text="{condition}" font="13" color="#888888" align="center"/>
      </card>
    </row>
  </screen>

  <screen id="settings" bg="#F5F5F5">
    <settings_form/>
  </screen>

</screens>
"""

# ─── HTTP Handler ─────────────────────────────────────────────────────────────

class Handler(BaseHTTPRequestHandler):

    def send_json(self, data, status=200):
        body = json.dumps(data, ensure_ascii=False, indent=2).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", len(body))
        self.send_header("X-Layout-Version", SPEC_VERSION)
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(body)

    def send_xml(self, xml_str):
        body = xml_str.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/xml; charset=utf-8")
        self.send_header("Content-Length", len(body))
        self.send_header("X-Layout-Version", SPEC_VERSION)
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        now = datetime.now(TZ)
        ts  = now.strftime("%H:%M:%S")

        if self.path == "/data.json":
            print(f"[{ts}] GET /data.json")
            data = get_cached_data()
            if data is None:
                _refresh_cache()
                data = get_cached_data()
            self.send_json(data)

        elif self.path == "/layout.xml":
            print(f"[{ts}] GET /layout.xml")
            self.send_xml(LAYOUT_XML)

        elif self.path == "/health":
            self.send_json({"status": "ok", "version": SPEC_VERSION,
                            "timestamp": now.isoformat()})

        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, fmt, *args):
        pass  # silenced, we handle logging manually


# ─── Main ─────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    server = HTTPServer(("0.0.0.0", PORT), Handler)
    print(f"🚀 SenseCAP Display Server v{SPEC_VERSION}")
    print(f"   http://0.0.0.0:{PORT}/data.json")
    print(f"   http://0.0.0.0:{PORT}/layout.xml")
    print(f"   http://0.0.0.0:{PORT}/health")
    print(f"   Ctrl+C to stop\n")
    _schedule_refresh()  # start background cache refresh
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
