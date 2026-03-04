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
import os
from datetime import datetime, timedelta, date
import calendar as _cal_mod
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

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
SPEC_VERSION = "1.3.81"
TZ = pytz.timezone("Europe/Rome")
CALDAV_USER = "mail@sromano.com"

DAYS_IT   = ["Lunedì","Martedì","Mercoledì","Giovedì","Venerdì","Sabato","Domenica"]

# ─── Background cache ─────────────────────────────────────────────────────────
_cache_lock = threading.Lock()
_cached_data = None
_cache_ts    = 0
CACHE_TTL    = 60  # seconds

# Home message generation cache (periodic slots + manual refresh)
_home_message_cache = None
_home_message_ai = False
_home_message_generated_at = None
_HOME_CACHE_FILE = os.path.join(os.path.dirname(__file__), "home_message_cache.json")


def _save_home_message_cache():
    try:
        if not _home_message_cache or not _home_message_generated_at:
            return
        payload = {
            "message": _home_message_cache,
            "is_ai": bool(_home_message_ai),
            "generated_ts": int(_home_message_generated_at.timestamp()),
        }
        with open(_HOME_CACHE_FILE, "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False)
    except Exception as e:
        print(f"[home-cache] save error: {e}")


def _load_home_message_cache():
    global _home_message_cache, _home_message_ai, _home_message_generated_at
    try:
        if not os.path.exists(_HOME_CACHE_FILE):
            return
        with open(_HOME_CACHE_FILE, "r", encoding="utf-8") as f:
            payload = json.load(f)
        msg = (payload.get("message") or "").strip()
        ts = int(payload.get("generated_ts") or 0)
        if not msg or ts <= 0:
            return
        _home_message_cache = msg
        _home_message_ai = bool(payload.get("is_ai", False))
        _home_message_generated_at = datetime.fromtimestamp(ts, TZ)
        print(f"[home-cache] loaded ({_home_message_generated_at.isoformat()})")
    except Exception as e:
        print(f"[home-cache] load error: {e}")


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


def get_presence():
    try:
        out = subprocess.run(["arp", "-a"], capture_output=True, text=True, timeout=2).stdout.lower()
    except Exception:
        out = ""
    def present(mac):
        m = mac.lower()
        return (m in out) and (f"({m})" not in out or "incomplete" not in out)
    simone = present("ec:28:d3:1b:9d:77")
    roberta = present("88:1e:5a:a3:2f:57")
    names = []
    if simone: names.append("Simone")
    if roberta: names.append("Roberta")
    return {
        "in_home": names,
        "in_home_text": ", ".join(names) if names else "Nessuno",
        "simone_home": simone,
        "roberta_home": roberta,
    }

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
        temp = cur["temperature_2m"]
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

        def rain_prob_window(h_start, h_end, day_offset=0):
            probs = []
            target = (now + timedelta(days=day_offset)).date()
            for i, t in enumerate(hours):
                dt = datetime.fromisoformat(t)
                if dt.date() == target and h_start <= dt.hour < h_end:
                    probs.append(prec[i])
            return max(probs) if probs else 0

        rain_morning = rain_prob_window(8, 9)    # 8:30-9:00 commute
        rain_evening = rain_prob_window(17, 18)  # 17:30-18:00 commute
        rain_max_today   = rain_prob_window(6, 22)
        rain_max_tomorrow= rain_prob_window(6, 22, day_offset=1)

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
            "outdoor_temp": f"{temp:.1f}°C",
            "outdoor_hum":  f"{hum}%",
            "condition":    condition,
            "condition_icon": icon,
            "wind":         f"{wind} km/h",
            "rain_morning": rain_morning,
            "rain_evening": rain_evening,
            "rain_max_today": rain_max_today,
            "rain_max_tomorrow": rain_max_tomorrow,
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
            "rain_max_today": 0,
            "rain_max_tomorrow": 0,
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

        # Parse blocks: each entry starts with a date repeated twice
        blocks = re.split(r"(?=\d{2}/\d{2}/\d{4}\s+\d{2}/\d{2}/\d{4})", text)

        hits = []
        seen = set()
        for block in blocks:
            date_m = re.match(r"(\d{2}/\d{2}/\d{4})", block)
            if not date_m: continue
            try:
                date_str = date_m.group(1)
                d, mo, y = int(date_str[:2]), int(date_str[3:5]), int(date_str[6:])
                ev_date = _date(y, mo, d)
            except: continue
            if ev_date <= today: continue
            date_it = f"{d} {MONTHS_IT[mo]}"
            bu = block.upper()

            # ATM Milano
            if "ATM" in bu and ("MILANO" in bu or "GRUPPO ATM" in bu):
                dur_m = re.search(r"(\d+) ORE|INTERA GIORNATA", bu)
                dur_str = "24h" if dur_m and ("24" in dur_m.group(0) or "INTERA" in dur_m.group(0)) else \
                          (f"{dur_m.group(1)}h" if dur_m else "")
                line = f"{date_it}: ATM Milano - {dur_str}".rstrip(" -")
            # Sciopero generale (che include trasporti)
            elif "GENERALE" in bu and "ESCLUSIONE" not in bu and "ESCLUSO" not in bu and "TRASPORT" not in bu:
                line = f"{date_it}: sciopero generale"
            elif re.search(r"FERROVI|NTV|TRENITALIA|TRENORD", bu):
                if "NAZIONALE" in bu: line = f"{date_it}: treni"
                else: continue
            elif "AEREO" in bu or "AIRWAYS" in bu or "HANDLING" in bu:
                if "NAZIONALE" in bu: line = f"{date_it}: aerei"
                else: continue
            else:
                continue

            if line not in seen:
                seen.add(line)
                hits.append((ev_date, line))

        hits.sort(key=lambda x: x[0])
        # Deduplicate by line, always include all ATM entries
        seen = set(); result = []
        # First pass: ATM entries
        for ev_date, line in hits:
            if "ATM" in line and line not in seen:
                seen.add(line); result.append(line)
        # Second pass: others (one per category)
        cat_seen = set()
        for ev_date, line in hits:
            if "ATM" in line: continue
            cat = line.split(": ", 1)[-1]
            if cat not in cat_seen and line not in seen:
                cat_seen.add(cat); seen.add(line); result.append(line)
        result.sort(key=lambda l: next((ev_date for ev_date,ln in hits if ln==l), _date(2099,1,1)))
        if result:
            return result[:5]
        # Fallback: Google News RSS titles
        fallback = _fetch_rss("https://news.google.com/rss/search?q=sciopero+ATM+Milano+metro&hl=it&gl=IT&ceid=IT:it", skip=2, limit=3)
        return [strip_emoji(t[:80]) for t in fallback if any(w in t.lower() for w in ["atm","metro","sciopero"])][:3]
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
        "scioperi":[strip_emoji(t) for t in scioperi],
    }

MONTHS_IT_FULL = ["Gennaio","Febbraio","Marzo","Aprile","Maggio","Giugno",
                   "Luglio","Agosto","Settembre","Ottobre","Novembre","Dicembre"]
DAYS_IT_FULL   = ["Lunedì","Martedì","Mercoledì","Giovedì","Venerdì","Sabato","Domenica"]

def _fmt_event_date(dtstart, dtend):
    """Return 'Martedì 3 Marzo, 19:00 - 20:00' or 'Martedì 3 Marzo' for all-day."""
    if isinstance(dtstart, date) and not isinstance(dtstart, datetime):
        wd = DAYS_IT_FULL[dtstart.weekday()]
        mo = MONTHS_IT_FULL[dtstart.month-1]
        return f"{wd} {dtstart.day} {mo}"
    wd = DAYS_IT_FULL[dtstart.weekday()]
    mo = MONTHS_IT_FULL[dtstart.month - 1]
    day = dtstart.day
    t1 = dtstart.strftime("%H:%M")
    if dtend and isinstance(dtend, datetime):
        t2 = dtend.strftime("%H:%M")
        return f"{wd} {day} {mo}, {t1} - {t2}"
    return f"{wd} {day} {mo}, {t1}"

def get_events():
    try:
        pwd = caldav_password()
        client = caldav.DAVClient(
            url="https://caldav.icloud.com",
            username=CALDAV_USER, password=pwd)
        cal = next(c for c in client.principal().calendars() if c.name == "iCloud")
        now   = datetime.now(TZ)
        start = now.replace(hour=0, minute=0, second=0, microsecond=0)
        end   = start + timedelta(days=14)
        evts  = cal.search(start=start, end=end, event=True, expand=True)
        result = []
        for e in evts:
            try:
                v     = e.vobject_instance.vevent
                title = str(v.summary.value)
                dtstart = v.dtstart.value
                dtend   = v.dtend.value if hasattr(v, 'dtend') else None
                # normalize to local TZ
                if isinstance(dtstart, datetime):
                    if dtstart.tzinfo:
                        dtstart = dtstart.astimezone(TZ)
                    if isinstance(dtend, datetime) and dtend.tzinfo:
                        dtend = dtend.astimezone(TZ)
                date_str = _fmt_event_date(dtstart, dtend)
                sort_key = dtstart.isoformat() if hasattr(dtstart, 'isoformat') else str(dtstart)
                result.append({"sort": sort_key, "date_str": date_str, "title": title, "dtstart": dtstart})
            except: pass
        result.sort(key=lambda x: x["sort"])
        # Return as "TITLE|||date_str" strings
        return [f"{r['title']}|||{r['date_str']}" for r in result[:15]]
    except:
        return []

# ---- Heating (Shelly BluTRV) ----
_heating_last_action_ts = 0
_calendar_offset_months = 0

HEAT_ROOMS = {
    "sala":   ("192.168.1.33", 200),
    "cucina": ("192.168.1.33", 201),
    "camera": ("192.168.1.33", 202),
    "bagno":  ("192.168.1.34", 200),
    "studio": ("192.168.1.34", 201),
}

def _heat_get_status(ip, rid):
    try:
        u = f"http://{ip}/rpc/BluTRV.GetStatus?id={rid}"
        req = urllib.request.Request(u, headers={"User-Agent":"Mozilla/5.0"})
        with urllib.request.urlopen(req, timeout=3.0) as r:
            return json.loads(r.read().decode("utf-8", errors="replace"))
    except Exception:
        return None

def _heat_set_target(ip, rid, target_c):
    try:
        u = f"http://{ip}/rpc/BluTRV.Call"
        payload = json.dumps({"id": rid, "method": "TRV.SetTarget", "params": {"id": 0, "target_C": float(target_c)}}).encode("utf-8")
        req = urllib.request.Request(u, data=payload, headers={"Content-Type":"application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=4.0) as r:
            _ = r.read()
        return True
    except Exception:
        return False


def _add_months(dt, months):
    y = dt.year + (dt.month - 1 + months) // 12
    m = (dt.month - 1 + months) % 12 + 1
    d = min(dt.day, _cal_mod.monthrange(y, m)[1])
    return dt.replace(year=y, month=m, day=d)

def get_heating():
    out = {}
    on_cnt = 0
    total = 0
    for name, (ip, rid) in HEAT_ROOMS.items():
        st = _heat_get_status(ip, rid)
        total += 1
        if st is None:
            out[f"heat_{name}"] = "--"
            continue
        tgt = st.get("target_C", 0)
        is_on = isinstance(tgt, (int, float)) and tgt >= 20
        out[f"heat_{name}"] = "Acceso" if is_on else "Spento"
        # extra telemetry for UI cards
        batt = None
        try:
            if isinstance(st.get("battery"), dict):
                batt = st.get("battery", {}).get("percent")
            if batt is None:
                batt = st.get("battery_percent")
        except Exception:
            batt = None
        out[f"heat_{name}_battery"] = f"{int(batt)}%" if isinstance(batt, (int, float)) else "--"
        out[f"heat_{name}_target"] = (f"{float(tgt):.1f}°C" if isinstance(tgt, (int, float)) and tgt > 0 else "--")
        if is_on:
            on_cnt += 1
    if on_cnt == 0:
        out["heat_global"] = "Tutto spento"
    elif on_cnt == total:
        out["heat_global"] = "Tutto acceso"
    else:
        out["heat_global"] = f"Misto ({on_cnt}/{total} accesi)"
    out["heat_global_paren"] = f"({out['heat_global']})"
    return out

def heating_action(cmd):
    global _heating_last_action_ts
    cmd = (cmd or "").strip().lower()

    def _mark(ok: bool) -> bool:
        global _heating_last_action_ts
        if ok:
            _heating_last_action_ts = int(datetime.now(TZ).timestamp())
        return ok

    if cmd == "all_on":
        return _mark(all(_heat_set_target(ip, rid, 30) for ip, rid in HEAT_ROOMS.values()))
    if cmd == "all_off":
        return _mark(all(_heat_set_target(ip, rid, 4) for ip, rid in HEAT_ROOMS.values()))
    for name, (ip, rid) in HEAT_ROOMS.items():
        if cmd == f"{name}_on":
            return _mark(_heat_set_target(ip, rid, 30))
        if cmd == f"{name}_off":
            return _mark(_heat_set_target(ip, rid, 4))
    return False


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



def _build_home_message_fallback(now, weather, news, crypto, events, curiosity):
    fc = weather.get("forecast", [])
    parts = []

    parts.append("# Meteo")
    parts.append(f"{weather['condition']}, {weather['outdoor_temp']}, vento {weather['wind']}")
    if fc:
        t = fc[0]
        precip = t.get("precip", 0) or 0
        prob = weather.get("rain_max_today", 0)
        rain = f"pioggia {prob}% ({precip:.0f}mm)" if (prob >= 20 or precip >= 1) else "nessuna pioggia"
        parts.append(f"- **Oggi** {t['desc']}, {t['max']}°/{t['min']}°C - {rain}")
    if len(fc) > 1:
        t = fc[1]
        precip = t.get("precip", 0) or 0
        prob = weather.get("rain_max_tomorrow", 0)
        rain = f"pioggia {prob}% ({precip:.0f}mm)" if (prob >= 20 or precip >= 1) else "nessuna pioggia"
        parts.append(f"- **Domani** {t['desc']}, {t['max']}°/{t['min']}°C - {rain}")

    roberta = build_roberta_note(now, weather["rain_morning"], weather["rain_evening"])
    if roberta:
        parts.append("")
        parts.append("## Commute Roberta")
        for line in roberta.split("\n"):
            parts.append(f"- {line}")

    if news.get("scioperi"):
        parts.append("")
        parts.append("## Scioperi")
        for item in news["scioperi"][:5]:
            parts.append(f"- {item}")

    if news.get("italia"):
        parts.append("")
        parts.append("## Notizie Italia")
        for item in news["italia"][:3]:
            parts.append(f"- {item}")

    if news.get("estero"):
        parts.append("")
        parts.append("## Notizie Estero")
        for item in news["estero"][:3]:
            parts.append(f"- {item}")

    parts.append("")
    parts.append("## Mercati")
    parts.append(f"- BTC {crypto['btc']['price']}  **{crypto['btc']['change']}**")
    parts.append(f"- ETH {crypto['eth']['price']}  **{crypto['eth']['change']}**")
    parts.append(f"- IOTX {crypto['iotx']['price']}  **{crypto['iotx']['change']}**")

    today_date = now.strftime("%-d %b").lower()
    today_events = [e for e in events if isinstance(e, str) and today_date in e.lower()]
    if today_events:
        parts.append("")
        parts.append("## Agenda di oggi")
        for e in today_events:
            title = e.split("|||")[0] if "|||" in e else e
            date_s = e.split("|||")[1] if "|||" in e else ""
            parts.append(f"- **{title}**" + (f" - {date_s}" if date_s else ""))

    if curiosity:
        parts.append("")
        parts.append("## Curiosità")
        parts.append(curiosity)

    return "\n".join(parts)


def _generate_home_message_bridge(now, weather, news, crypto, events, curiosity):
    cmd = os.getenv("HOME_MESSAGE_BRIDGE_CMD", "").strip()
    if not cmd:
        return None

    heating = get_heating()
    presence = get_presence()
    payload = {
        "now": now.isoformat(),
        "weather": weather,
        "heating": heating,
        "presence": presence,
        "news": news,
        "crypto": crypto,
        "events": events[:10],
        "curiosity": curiosity,
        "rules": {
            "format": "markdown-lite",
            "allowed": ["#", "##", "###", "**bold**", "*italic*", "_italic_", "- bullets"],
            "max_chars": 2400,
            "lang": "it"
        }
    }

    try:
        proc = subprocess.run(
            cmd,
            input=json.dumps(payload, ensure_ascii=False),
            text=True,
            shell=True,
            capture_output=True,
            timeout=int(os.getenv("HOME_MESSAGE_BRIDGE_TIMEOUT", "40")),
        )
        if proc.returncode != 0:
            err = (proc.stderr or "").strip()
            print(f"[bridge] cmd failed rc={proc.returncode}: {err[:200]}")
            return None
        out = (proc.stdout or "").strip()
        return out or None
    except Exception as e:
        print(f"[bridge] generation failed: {e}")
        return None


def _generate_home_message_ai(now, weather, news, crypto, events, curiosity):
    api_key = os.getenv("OPENAI_API_KEY", "").strip()
    if not api_key:
        return None

    model = os.getenv("HOME_MESSAGE_AI_MODEL", "gpt-4o-mini")
    max_events = []
    for e in events[:6]:
        if isinstance(e, str):
            max_events.append(e)

    prompt = {
        "now": now.isoformat(),
        "weather": weather,
        "news": news,
        "crypto": crypto,
        "events": max_events,
        "curiosity": curiosity,
        "rules": [
            "Write in Italian",
            "Output ONLY markdown text",
            "Use markdown subset: # ## ###, **bold**, *italic*, - bullets",
            "No code fences, no tables",
            "Keep it concise and readable on a small display"
        ]
    }

    body = {
        "model": model,
        "input": [
            {"role": "system", "content": "Sei un assistente che compone un breve messaggio home dashboard in markdown."},
            {"role": "user", "content": json.dumps(prompt, ensure_ascii=False)}
        ],
        "max_output_tokens": 500
    }

    req = urllib.request.Request(
        "https://api.openai.com/v1/responses",
        data=json.dumps(body).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json"
        },
        method="POST"
    )

    try:
        with urllib.request.urlopen(req, timeout=6) as r:
            res = json.loads(r.read().decode("utf-8", errors="replace"))
        text = (res.get("output_text") or "").strip()
        return text or None
    except Exception as e:
        print(f"[ai] home_message generation failed: {e}")
        return None


def _style_home_message_ai(raw_text):
    text = (raw_text or "").strip()
    if not text:
        return None

    # Optional bridge-style formatter command
    style_cmd = os.getenv("HOME_MESSAGE_STYLE_BRIDGE_CMD", "").strip()
    if style_cmd:
        payload = {
            "task": "format_home_message_markdown_lite",
            "input": text,
            "rules": {
                "keep_facts": True,
                "language": "it",
                "format": "markdown-lite",
                "allowed": ["#", "##", "###", "**bold**", "*italic*", "_italic_", "- bullets"],
                "no_tables": True,
                "no_code_fences": True,
                "max_chars": 2600
            }
        }
        try:
            proc = subprocess.run(
                style_cmd,
                input=json.dumps(payload, ensure_ascii=False),
                text=True,
                shell=True,
                capture_output=True,
                timeout=int(os.getenv("HOME_MESSAGE_STYLE_BRIDGE_TIMEOUT", "20")),
            )
            if proc.returncode == 0:
                out = (proc.stdout or "").strip()
                if out:
                    return out
            else:
                print(f"[style-bridge] rc={proc.returncode}: {(proc.stderr or '').strip()[:180]}")
        except Exception as e:
            print(f"[style-bridge] failed: {e}")

    # Fallback: OpenAI formatting pass (if key available)
    api_key = os.getenv("OPENAI_API_KEY", "").strip()
    if not api_key:
        return None

    model = os.getenv("HOME_MESSAGE_STYLE_MODEL", os.getenv("HOME_MESSAGE_AI_MODEL", "gpt-4o-mini"))
    prompt = {
        "instruction": "Riformatta il testo in markdown-lite per display piccolo, senza inventare fatti.",
        "rules": [
            "Mantieni tutti i fatti principali",
            "Italiano naturale",
            "Usa # ## ### per sezioni",
            "Usa - per liste",
            "Usa **bold** e *italic* solo dove utile",
            "No tabelle, no blocchi di codice"
        ],
        "text": text
    }
    body = {
        "model": model,
        "input": [
            {"role": "system", "content": "Sei un formatter markdown-lite per dashboard."},
            {"role": "user", "content": json.dumps(prompt, ensure_ascii=False)}
        ],
        "max_output_tokens": 500
    }
    req = urllib.request.Request(
        "https://api.openai.com/v1/responses",
        data=json.dumps(body).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json"
        },
        method="POST"
    )
    try:
        with urllib.request.urlopen(req, timeout=6) as r:
            res = json.loads(r.read().decode("utf-8", errors="replace"))
        out = (res.get("output_text") or "").strip()
        return out or None
    except Exception as e:
        print(f"[style-ai] failed: {e}")
        return None


def build_home_message(now, weather, news, crypto, events, curiosity):
    bridge_msg = _generate_home_message_bridge(now, weather, news, crypto, events, curiosity)
    if bridge_msg:
        raw = strip_emoji(bridge_msg)
        pretty = _style_home_message_ai(raw)
        return strip_emoji(pretty or raw), True

    ai_msg = _generate_home_message_ai(now, weather, news, crypto, events, curiosity)
    if ai_msg:
        raw = strip_emoji(ai_msg)
        pretty = _style_home_message_ai(raw)
        return strip_emoji(pretty or raw), True

    return strip_emoji(_build_home_message_fallback(now, weather, news, crypto, events, curiosity)), False


def _home_slot_boundaries(now):
    # Daily slots: 07:00, 12:00, 19:00
    s1 = now.replace(hour=7, minute=0, second=0, microsecond=0)
    s2 = now.replace(hour=12, minute=0, second=0, microsecond=0)
    s3 = now.replace(hour=19, minute=0, second=0, microsecond=0)
    return [s1, s2, s3]

def _home_message_needs_refresh(now):
    global _home_message_generated_at, _home_message_cache
    if _home_message_cache is None or _home_message_generated_at is None:
        return True
    # refresh if crossed one of the slot boundaries after last generation
    for slot in _home_slot_boundaries(now):
        if _home_message_generated_at < slot <= now:
            return True
    return False

def _format_age_label(now, then):
    if not then:
        return "--"
    try:
        delta = int((now - then).total_seconds())
    except Exception:
        return "--"
    if delta < 10:
        return "adesso"
    if delta < 120:
        return f"{(delta // 10) * 10}s fa"
    if delta < 3600:
        return f"{delta // 60}m fa"
    return f"{delta // 3600}h fa"

def _home_message_get(now, weather, news, crypto, events, curiosity, force=False):
    global _home_message_cache, _home_message_ai, _home_message_generated_at
    if force or _home_message_needs_refresh(now):
        msg, is_ai = build_home_message(now, weather, news, crypto, events, curiosity)
        _home_message_cache = msg
        _home_message_ai = is_ai
        _home_message_generated_at = datetime.now(TZ)
        _save_home_message_cache()
    return _home_message_cache, _home_message_ai


# ─── Data builder ─────────────────────────────────────────────────────────────

def build_data():
    now     = datetime.now(TZ)
    cal_ref = _add_months(now, _calendar_offset_months)
    cal_year = cal_ref.year
    cal_month = cal_ref.month
    weather = get_weather()
    heating = get_heating()
    crypto  = get_crypto()
    news     = get_news()
    events   = get_events()
    curiosity = get_curiosity(now)
    home_message, home_ai_generated = _home_message_get(now, weather, news, crypto, events, curiosity, force=False)

    if now.hour < 12:
        home_title = "Buongiorno"
    elif now.hour < 18:
        home_title = "Buon pomeriggio"
    else:
        home_title = "Buonasera"

    # event_days
    ev_days = set()
    for ev in events:
        try:
            date_part = ev.split("|||")[1] if "|||" in ev else ""
            for mi, mn in enumerate(MONTHS_IT_FULL):
                if mn in date_part:
                    rest = date_part.replace(mn, "").strip()
                    day_num = int(rest.split(",")[0].split()[0])
                    if mi + 1 == cal_month and now.year == cal_year:
                        ev_days.add(day_num)
                    break
        except Exception:
            pass
    event_days_str = ",".join(str(d) for d in sorted(ev_days))

    # holiday_days
    hol_days = set()
    for day_num in range(1, 32):
        try:
            dt = datetime(cal_year, cal_month, day_num).date()
            is_sunday = (dt.weekday() == 6)
            is_weekday_hol = is_italian_holiday(dt) and dt.weekday() < 5
            if is_sunday or is_weekday_hol:
                hol_days.add(day_num)
        except Exception:
            pass
    holiday_days_str = ",".join(str(d) for d in sorted(hol_days))

    return {
        "_version":   SPEC_VERSION,
        "updated_at": now.strftime("%H:%M"),
        "updated_ts": int(now.timestamp()),
        "event_days":   event_days_str,
        "holiday_days": holiday_days_str,
        "_timestamp": now.isoformat(),

        "outdoor_temp":    weather["outdoor_temp"],
        "outdoor_hum":     weather["outdoor_hum"],
        "condition":       weather["condition"],
        "condition_icon":  weather["condition_icon"],

        **heating,
        "heat_action_ts": str(_heating_last_action_ts),

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

        "events":    events,
        "home_title": home_title,
        "home_ai_badge": "AI" if home_ai_generated else "",
        "home_message_generated_ts": str(int(_home_message_generated_at.timestamp())) if _home_message_generated_at else "0",
        "home_message": strip_emoji(home_message),
        "home_lines": [strip_emoji(x) for x in home_message.split("\n")],
        "month_name": MONTHS_IT[now.month - 1],
        "weekday_long": DAYS_IT_FULL[now.weekday()],
        "year":        str(now.year),
        "clock_date":  f"{DAYS_IT_FULL[now.weekday()]}, {now.day} {MONTHS_IT[now.month-1]} {now.year}",
        "cal_header":  f"{MONTHS_IT[cal_month-1]} {cal_year}",
        "cal_year":     str(cal_year),
        "cal_month":    str(cal_month),
        "cal_today":    str(now.day if (cal_year==now.year and cal_month==now.month) else 0),
        "cal_startdow": str(_cal_mod.monthrange(cal_year, cal_month)[0]),
        "cal_days":     str(_cal_mod.monthrange(cal_year, cal_month)[1]),
        "voc":       "--",
        "co2":       "--",
        "alert":     "",
        "day_color":  "#E53935" if is_italian_holiday(now) else "#1A1A2E",
    }

# ─── Layout XML (light theme) ─────────────────────────────────────────────────

LAYOUT_XML = """<?xml version="1.0" encoding="UTF-8"?>
<screens version="1.5.20">

  <screen id="home" bg="#4A235A" grad_color="#1B4F72" pad="10">
    <row gap="10" h="310">
      <!-- Left col: Interno + Esterno -->
      <col flex="1" gap="10">
        <card bg="#000" bg_opa="30" border_color="#FFFFFF" border_width="1" radius="12" pad="16" flex="1">
          <label text="Interno" font="18" color="#CCCCEE" bold="true" align="center"/>
          <label text="{indoor_temp}" font="18" color="#FFFFFF" align="center" bold="true"/>
          <label text="{indoor_hum}" font="16" color="#AAAACC" align="center"/>
        </card>
        <card bg="#000" bg_opa="30" border_color="#FFFFFF" border_width="1" radius="12" pad="16" flex="1">
          <label text="Esterno" font="18" color="#CCCCEE" bold="true" align="center"/>
          <label text="{outdoor_temp}" font="18" color="#FFFFFF" align="center" bold="true"/>
          <label text="{outdoor_hum}" font="16" color="#AAAACC" align="center"/>
        </card>
      </col>
      <!-- Center: tall date card -->
      <card bg="#000" bg_opa="30" border_color="#FFFFFF" border_width="1" radius="12" pad_h="8" pad_v="8" gap="4" tight="true" flex="2" h="100%" valign="center">
        <label text="{weekday}" font="28" color="#CCCCEE" align="center"/>
        <label text="{day}" font="128" color="#FFFFFF" align="center" bold="true"/>
        <label text="{month_name}" font="28" color="#CCCCEE" align="center"/>
        <row gap="0" h="26">
          <label text="{time}" font="22" bold="true" color="#FFFFFF" align="center"/>
          <label text=".{time_ss}" font="16" color="#DDDDFF"/>
        </row>
      </card>
      <!-- Right col: tVOC + CO2 -->
      <col flex="1" gap="10">
        <card bg="#000" bg_opa="30" border_color="#FFFFFF" border_width="1" radius="12" pad="16" flex="1" valign="center">
          <label text="tVOC" font="18" color="#CCCCEE" bold="true" align="center"/>
          <label text="{voc}" font="18" color="#FFFFFF" align="center" bold="true"/>
          <label text="idx" font="16" color="#AAAACC" align="center"/>
        </card>
        <card bg="#000" bg_opa="30" border_color="#FFFFFF" border_width="1" radius="12" pad="16" flex="1">
          <label text="CO2" font="18" color="#CCCCEE" bold="true" align="center"/>
          <label text="{co2}" font="18" color="#FFFFFF" align="center" bold="true"/>
          <label text="{co2_unit}" font="16" color="#AAAACC" align="center"/>
        </card>
      </col>
    </row>
    <row gap="8" w="100%">
      <label text="agg. {data_age}" font="12" color="#CCCCEE" align="left" flex="1"/>
      <label text="▶ update" font="12" color="#AAB0CC" w="auto"/>
      <label text="{home_ai_badge}" font="12" color="#AAB0CC" w="auto"/>
    </row>
    <!-- Bottom: scrollable full buongiorno -->
    <card bg="#000" bg_opa="30" border_color="#FFFFFF" border_width="1" radius="12" pad="14" w="100%" scroll="true" scrollbar="false" gap="10">
      <list items="{home_lines}" font="18" color="#D9D9EE" markdown="true" rich="true" bullet="" max_lines="0"/>
    </card>
    <card bg="#E53935" radius="6" pad="12" w="100%" visible="{alert_visible}">
      <label text="⚠ {alert}" font="16" color="#FFFFFF" align="center"/>
    </card>
  </screen>

  <screen id="heating" bg="#4A235A" grad_color="#1B4F72" pad="10">
    <heating_controls/>
  </screen>

  <screen id="calendar" bg="#F0F0F6" pad="10">
    <calendar_nav/>
    <!-- Header: mese a sx, temp int/est a dx -->
    <row gap="4" h="28" w="100%">
      <label text="{cal_header}" font="14" bold="true" color="#1A1A2E" flex="1"/>
      <label text="Int: " font="14" color="#666666" w="auto"/>
      <label text="{indoor_temp}" font="14" color="#5B21B6" bold="false" w="auto"/>
      <label text="  Est: " font="14" color="#666666" w="auto"/>
      <label text="{outdoor_temp}" font="14" color="#5B21B6" bold="false" w="auto"/>
    </row>
    <calendar_grid today="{cal_today}" startdow="{cal_startdow}" days="{cal_days}" event_days="{event_days}" holiday_days="{holiday_days}"
      highlight_color="#D63384" text_color="#1A1A2E" header_color="#888888" cell_bg="#EFEFEF"/>
    <card bg="#FFFFFF" bg_opa="220" border_color="#FFFFFF" border_width="2" radius="6" pad="12" w="100%" scroll="true" scrollbar="false">
      <label text="Prossimi eventi" font="16" color="#5B21B6" bold="true"/>
      <events_list items="{events}" font="15" color="#1A1A2E" date_color="#555555"/>
    </card>
  </screen>

  <screen id="clock" bg="#4A235A" grad_color="#1B4F72" pad="12">
    <!-- Card orologio -->
    <label text="{time_sec}" font="20" color="#CCCCEE" align="center"/>
    <card bg="#FFFFFF" bg_opa="30" border_color="#FFFFFF" border_width="1" radius="12" pad="16" w="100%" gap="4">
      <big_clock font="96" color="#FFFFFF" align="center" format="HH:MM" bold="true"/>
      <label text="{clock_date}" font="24" color="#CCCCEE" align="center"/>
    </card>
    <!-- Card temperature -->
    <row gap="12" w="100%">
      <card flex="1" bg="#FFFFFF" bg_opa="30" border_color="#FFFFFF" border_width="1" radius="12" pad="16">
        <label text="▲ Interno" font="13" color="#CCCCEE" align="center"/>
        <label text="{indoor_temp}" font="32" color="#FFFFFF" align="center" bold="true"/>
        <label text="{indoor_hum}" font="14" color="#AAAACC" align="center"/>
      </card>
      <card flex="1" bg="#FFFFFF" bg_opa="30" border_color="#FFFFFF" border_width="1" radius="12" pad="16">
        <label text="☁ Esterno" font="13" color="#CCCCEE" align="center"/>
        <label text="{outdoor_temp}" font="32" color="#FFFFFF" align="center" bold="true"/>
        <label text="{condition}" font="13" color="#AAAACC" align="center"/>
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

        elif self.path.startswith("/calendar/nav"):
            print(f"[{ts}] GET /calendar/nav")
            global _calendar_offset_months
            try:
                from urllib.parse import urlparse, parse_qs
                q = parse_qs(urlparse(self.path).query)
                cmd = (q.get("cmd", [""])[0])
                if cmd == "prev": _calendar_offset_months -= 1
                elif cmd == "next": _calendar_offset_months += 1
                elif cmd == "today": _calendar_offset_months = 0
                self.send_json({"ok": True, "offset": _calendar_offset_months}, status=200)
            except Exception:
                self.send_json({"ok": False}, status=500)

        elif self.path.startswith("/heating/action"):
            print(f"[{ts}] GET /heating/action")
            ok = False
            try:
                from urllib.parse import urlparse, parse_qs
                q = parse_qs(urlparse(self.path).query)
                cmd = (q.get("cmd", [""])[0])
                ok = heating_action(cmd)
            except Exception:
                ok = False
            self.send_json({"ok": ok}, status=200 if ok else 500)

        elif self.path == "/home/refresh":
            print(f"[{ts}] GET /home/refresh")
            try:
                # Force regenerate home message now
                msg, is_ai = _home_message_get(now, get_weather(), get_news(), get_crypto(), get_events(), get_curiosity(now), force=True)
                # Rebuild cached payload immediately so clients see fresh age/message
                _refresh_cache()
                data = get_cached_data() or {}
                self.send_json({
                    "ok": True,
                    "home_ai": is_ai,
                    "home_preview": (msg or "")[:140]
                })
            except Exception as e:
                self.send_json({"ok": False, "error": str(e)}, status=500)

        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, fmt, *args):
        pass  # silenced, we handle logging manually


# ─── Main ─────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    print(f"🚀 SenseCAP Display Server v{SPEC_VERSION}")
    print(f"   http://0.0.0.0:{PORT}/data.json")
    print(f"   http://0.0.0.0:{PORT}/layout.xml")
    print(f"   http://0.0.0.0:{PORT}/health")
    print(f"   Ctrl+C to stop\n")
    _load_home_message_cache()
    _schedule_refresh()  # start background cache refresh
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")
