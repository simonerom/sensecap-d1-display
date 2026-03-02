// =============================================================================
// placeholder_engine.cpp
// =============================================================================
#include "placeholder_engine.h"
#include <time.h>
#include <Arduino.h>

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
        lv_label_set_text(label, it->second.c_str());
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

    // {time} HH:MM
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
    setValue("time", buf);

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
        "Domenica","Lunedi","Martedi","Mercoledi","Giovedi","Venerdi","Sabato"
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
}

// =============================================================================
// Sensor update
// =============================================================================
void PlaceholderEngine::updateSensor(float tempC, float humPct, bool ok) {
    // Sanity check: -45°C is SHT40 error sentinel, treat as not ok
    if (!ok || tempC < -40.0f || tempC > 85.0f || humPct < 0.0f || humPct > 100.0f) {
        setValue("indoor_temp", "--°C");
        setValue("indoor_hum",  "--%");
        return;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f°C", tempC);
    setValue("indoor_temp", buf);
    snprintf(buf, sizeof(buf), "%.0f%%", humPct);
    setValue("indoor_hum", buf);
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
                    result += it->second;
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
void PlaceholderEngine::_pushScalar(const String& key, const char* value) {
    auto it = _labels.find(key);
    if (it == _labels.end()) return;
    for (lv_obj_t* lbl : it->second) {
        if (lbl) lv_label_set_text(lbl, value);
    }
}

void PlaceholderEngine::_pushArray(const String& key, const std::vector<String>& items) {
    auto it = _arrays.find(key);
    if (it == _arrays.end()) return;
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
