// =============================================================================
// placeholder_engine.cpp
// =============================================================================
#include "placeholder_engine.h"
#include <time.h>
#include <Arduino.h>
#include <ctype.h>

PlaceholderEngine::PlaceholderEngine() {}

// =============================================================================
// Registration
// =============================================================================
void PlaceholderEngine::registerLabel(const char* key, lv_obj_t* label) {
    if (!key || !label) return;
    _labels[String(key)].push_back(label);
    // Set initial text if we already have a value
    auto it = _values.find(String(key));
    if (it != _values.end()) {
        String converted = _friendlyToLvgl(it->second);
        lv_label_set_text(label, converted.c_str());
    }
}

void PlaceholderEngine::registerArray(const char* key, lv_obj_t* container, ArrayRebuildFn fn) {
    if (!key || !container || !fn) return;
    _arrays[String(key)].push_back({container, fn});
}

void PlaceholderEngine::registerVisible(const char* key, lv_obj_t* obj) {
    if (!key || !obj) return;
    _visible[String(key)].push_back(obj);
}

void PlaceholderEngine::registerTrend(const char* key, TrendColorFn fn) {
    if (!key || !fn) return;
    _trends[String(key)].push_back(fn);
}

// =============================================================================
// Value updates
// =============================================================================
void PlaceholderEngine::setValue(const char* key, const char* value) {
    if (!key) return;
    String k(key);
    String v(value ? value : "");
    _values[k] = v;
    _pushScalar(k, v.c_str());
    _updateVisibility(k, v.c_str());
    _fireTrend(k, v.c_str());
}

void PlaceholderEngine::setArray(const char* key, const std::vector<String>& items) {
    if (!key) return;
    _pushArray(String(key), items);
}

void PlaceholderEngine::applyData(const std::map<String, String>& scalars,
                                   const std::map<String, std::vector<String>>& arrays) {
    _dataFetchedMs = millis();
    auto itTs = scalars.find(String("home_message_generated_ts"));
    if (itTs != scalars.end()) {
        _homeMsgGeneratedEpoch = (uint32_t) itTs->second.toInt();
    }
    for (auto& kv : scalars) {
        setValue(kv.first.c_str(), kv.second.c_str());
    }
    for (auto& kv : arrays) {
        setArray(kv.first.c_str(), kv.second);
    }
}

// =============================================================================
// RTC update — time.h struct tm (UTC + offset)
// =============================================================================
void PlaceholderEngine::updateRtc(int8_t tzOffset) {
    time_t now = time(nullptr) + (int32_t)tzOffset * 3600;
    struct tm t;
    gmtime_r(&now, &t);

    // {time} HH:MM and {time_sec} HH:MM:SS
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    setValue("time", buf);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    setValue("time_sec", buf);
    snprintf(buf, sizeof(buf), "%02d", t.tm_sec);
    setValue("time_ss", buf);

    // {date} "3 Mar"
    static const char* months[] = {
        "gen","feb","mar","apr","mag","giu",
        "lug","ago","set","ott","nov","dic"
    };
    snprintf(buf, sizeof(buf), "%d %s", t.tm_mday, months[t.tm_mon]);
    setValue("date", buf);

    // {day} — day-of-month number as string
    snprintf(buf, sizeof(buf), "%d", t.tm_mday);
    setValue("day", buf);

    // {weekday}
    static const char* weekdays[] = {
        "DOMENICA","LUNEDI","MARTEDI","MERCOLEDI","GIOVEDI","VENERDI","SABATO"
    };
    setValue("weekday", weekdays[t.tm_wday]);

    // {month}
    setValue("month", months[t.tm_mon]);

    // {cal_year}, {cal_month}, {cal_today}
    snprintf(buf, sizeof(buf), "%d", t.tm_year + 1900);
    setValue("cal_year", buf);
    snprintf(buf, sizeof(buf), "%d", t.tm_mon + 1);
    setValue("cal_month", buf);
    snprintf(buf, sizeof(buf), "%d", t.tm_mday);
    setValue("cal_today", buf);

    // {data_age} — elapsed since home message generation timestamp (preferred)
    if (_homeMsgGeneratedEpoch > 0) {
        time_t nowUtc = time(nullptr);
        long delta = (long)nowUtc - (long)_homeMsgGeneratedEpoch;
        if (delta < 0) delta = 0;
        char ageBuf[20];
        if (delta < 10) {
            snprintf(ageBuf, sizeof(ageBuf), "adesso");
        } else if (delta < 120) {
            long rounded = (delta / 10) * 10;
            snprintf(ageBuf, sizeof(ageBuf), "%lds fa", rounded);
        } else if (delta < 3600) {
            snprintf(ageBuf, sizeof(ageBuf), "%ldm fa", delta / 60);
        } else {
            snprintf(ageBuf, sizeof(ageBuf), "%ldh fa", delta / 3600);
        }
        setValue("data_age", ageBuf);
    } else if (_dataFetchedMs > 0) {
        // fallback when home timestamp is missing
        uint32_t elapsed = (millis() - _dataFetchedMs) / 1000;
        char ageBuf[16];
        if (elapsed < 10) {
            snprintf(ageBuf, sizeof(ageBuf), "adesso");
        } else if (elapsed < 120) {
            uint32_t rounded = (elapsed / 10) * 10;
            snprintf(ageBuf, sizeof(ageBuf), "%us fa", rounded);
        } else if (elapsed < 3600) {
            snprintf(ageBuf, sizeof(ageBuf), "%um fa", elapsed / 60);
        } else {
            snprintf(ageBuf, sizeof(ageBuf), "%uh fa", elapsed / 3600);
        }
        setValue("data_age", ageBuf);
    } else {
        setValue("data_age", "--");
    }
}

// =============================================================================
// Sensor update
// =============================================================================
void PlaceholderEngine::updateSensor(float tempC, float humPct, bool ok, float tvoc, float co2) {
    char buf[24];

    // T/H: sanity check (-40..85°C)
    if (!ok || tempC < -40.0f || tempC > 85.0f || humPct < 0.0f || humPct > 100.0f) {
        setValue("indoor_temp", "--");
        setValue("indoor_hum",  "--");
    } else {
        snprintf(buf, sizeof(buf), "%.1f°C", tempC);
        setValue("indoor_temp", buf);
        snprintf(buf, sizeof(buf), "%.0f%%", humPct);
        setValue("indoor_hum", buf);
    }

    // tVOC index from RP2040 — matches {voc} placeholder in layout
    if (tvoc <= 0) {
        setValue("voc", "--");
    } else {
        snprintf(buf, sizeof(buf), "%.0f", tvoc);
        setValue("voc", buf);
    }

    // CO2 from SCD41 via RP2040 — matches {co2} placeholder in layout
    if (co2 <= 0) {
        setValue("co2", "--");
    } else {
        snprintf(buf, sizeof(buf), "%.0f", co2);
        setValue("co2", buf);
    }
    setValue("co2_unit", co2 > 0 ? "ppm" : "");
}

// =============================================================================
// Resolve — substitute {key} tokens in a template string
// =============================================================================
String PlaceholderEngine::resolve(const char* templateStr) const {
    if (!templateStr) return String();
    String result;
    result.reserve(strlen(templateStr) + 16);
    const char* p = templateStr;
    while (*p) {
        if (*p == '{') {
            const char* end = strchr(p + 1, '}');
            if (end) {
                String key(p + 1, (unsigned)(end - p - 1));
                auto it = _values.find(key);
                if (it != _values.end()) {
                    result += _friendlyToLvgl(it->second);
                } else {
                    result += '{';
                    result += key;
                    result += '}';
                }
                p = end + 1;
                continue;
            }
        }
        result += *p++;
    }
    return result;
}

String PlaceholderEngine::get(const char* key) const {
    if (!key) return String();
    auto it = _values.find(String(key));
    return (it != _values.end()) ? it->second : String();
}

// =============================================================================
// clearRegistrations — clear widget registry (keep value cache)
// =============================================================================
void PlaceholderEngine::clearRegistrations() {
    _labels.clear();
    _visible.clear();
    _arrays.clear();
    _trends.clear();
}

// =============================================================================
// Internal helpers
// =============================================================================
String PlaceholderEngine::_friendlyToLvgl(const String& in) const {
    String out = in;

    // Normalize line endings
    out.replace("\r\n", "\n");
    out.replace("\r", "\n");

    // Markdown headers (#, ##, ###) + bullet list
    String normalized;
    int from = 0;
    while (from <= out.length()) {
        int nl = out.indexOf('\n', from);
        String line = (nl >= 0) ? out.substring(from, nl) : out.substring(from);

        if (line.startsWith("### ") ) {
            line = line.substring(4);
        } else if (line.startsWith("## ") ) {
            line = line.substring(3);
        } else if (line.startsWith("# ") ) {
            line = line.substring(2);
        }

        if (line.startsWith("- ") ) {
            line = String("• ") + line.substring(2);
        }

        normalized += line;
        if (nl < 0) break;
        normalized += "\n";
        from = nl + 1;
    }
    out = normalized;

    // {#RRGGBB}text{/} -> #RRGGBB text#
    int guard = 0;
    while (guard++ < 128) {
        int a = out.indexOf("{#");
        if (a < 0) break;
        int b = out.indexOf("}", a + 2);
        if (b < 0) break;
        int c = out.indexOf("{/}", b + 1);
        if (c < 0) break;
        String hex = out.substring(a + 2, b);
        if (hex.length() == 6) {
            String repl = out.substring(b + 1, c);
            out = out.substring(0, a) + repl + out.substring(c + 3);
        } else break;
    }

    // **bold**
    guard = 0;
    while (guard++ < 128) {
        int a = out.indexOf("**");
        if (a < 0) break;
        int b = out.indexOf("**", a + 2);
        if (b < 0) break;
        if (b == a + 2) { out.remove(a, 2); continue; }
        String inner = out.substring(a + 2, b);
        String repl = inner;
        out = out.substring(0, a) + repl + out.substring(b + 2);
    }

    auto isWord = [](char c) -> bool {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };

    // *italic*
    guard = 0;
    while (guard++ < 128) {
        int a = out.indexOf("*");
        if (a < 0) break;
        int b = out.indexOf("*", a + 1);
        if (b < 0) break;
        if (b == a + 1) { out.remove(a, 1); continue; }
        char prev = (a > 0) ? out[a - 1] : ' ';
        char next = (a + 1 < out.length()) ? out[a + 1] : ' ';
        char prev2 = (b > 0) ? out[b - 1] : ' ';
        char next2 = (b + 1 < out.length()) ? out[b + 1] : ' ';
        bool openOk = !isWord(prev) && !isspace((unsigned char)next);
        bool closeOk = !isspace((unsigned char)prev2) && !isWord(next2);
        if (!openOk || !closeOk) { out.remove(a, 1); continue; }
        String inner = out.substring(a + 1, b);
        String repl = inner;
        out = out.substring(0, a) + repl + out.substring(b + 1);
    }

    // _italic_
    guard = 0;
    while (guard++ < 128) {
        int a = out.indexOf("_");
        if (a < 0) break;
        int b = out.indexOf("_", a + 1);
        if (b < 0) break;
        if (b == a + 1) { out.remove(a, 1); continue; }
        char prev = (a > 0) ? out[a - 1] : ' ';
        char next = (a + 1 < out.length()) ? out[a + 1] : ' ';
        char prev2 = (b > 0) ? out[b - 1] : ' ';
        char next2 = (b + 1 < out.length()) ? out[b + 1] : ' ';
        bool openOk = !isWord(prev) && !isspace((unsigned char)next);
        bool closeOk = !isspace((unsigned char)prev2) && !isWord(next2);
        if (!openOk || !closeOk) { out.remove(a, 1); continue; }
        String inner = out.substring(a + 1, b);
        String repl = inner;
        out = out.substring(0, a) + repl + out.substring(b + 1);
    }

    return out;
}

void PlaceholderEngine::_pushScalar(const String& key, const char* value) {
    auto it = _labels.find(key);
    if (it == _labels.end()) return;
    String converted = _friendlyToLvgl(String(value ? value : ""));
    for (lv_obj_t* lbl : it->second) {
        if (lbl) lv_label_set_text(lbl, converted.c_str());
    }
}

void PlaceholderEngine::_pushArray(const String& key, const std::vector<String>& items) {
    auto it = _arrays.find(key);
    if (it == _arrays.end()) return;
    // Keep array items raw: list/widget renderers may apply markdown-aware formatting.
    for (auto& entry : it->second) {
        if (entry.container && entry.rebuildFn)
            entry.rebuildFn(entry.container, items);
    }
}

void PlaceholderEngine::_updateVisibility(const String& key, const char* value) {
    auto it = _visible.find(key);
    if (it == _visible.end()) return;
    // Visible if value is non-empty, "true", or "1"
    bool show = value && value[0] != '\0' &&
                strcmp(value, "false") != 0 &&
                strcmp(value, "0") != 0 &&
                strcmp(value, "none") != 0 &&
                strcmp(value, "-") != 0;
    for (lv_obj_t* obj : it->second) {
        if (!obj) continue;
        if (show) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(obj,   LV_OBJ_FLAG_HIDDEN);
    }
}

void PlaceholderEngine::_fireTrend(const String& key, const char* value) {
    auto it = _trends.find(key);
    if (it == _trends.end()) return;
    for (auto& fn : it->second) {
        if (fn) fn(String(value));
    }
}
