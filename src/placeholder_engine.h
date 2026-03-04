// =============================================================================
// placeholder_engine.h — Server-driven placeholder substitution system
//
// Maintains a registry of {key} → LVGL label lists.
// When a value changes, all registered labels are updated immediately.
//
// Three value sources:
//   - Local RTC: {time}, {date}, {day}, {weekday}, {month},
//                {cal_year}, {cal_month}, {cal_today}
//   - Grove sensor: {indoor_temp}, {indoor_hum}
//   - data.json fetch: everything else (scalar strings + arrays)
//
// Thread safety: all public methods MUST be called from taskUI (Core 1).
// The network task must post updates via ScreenManager::postDataUpdate().
// =============================================================================
#pragma once

#include <lvgl.h>
#include <Arduino.h>
#include <map>
#include <vector>
#include <functional>

// Callback type for array placeholder rebuilds.
// Called when a list/events/calendar array value changes.
// container: the parent LVGL object to repopulate.
// items:     new string list (for news, events flattened to strings, etc.)
using ArrayRebuildFn = std::function<void(lv_obj_t* container,
                                          const std::vector<String>& items)>;

// Callback for trend-colored labels (crypto change %)
// Called when {xxx_trend} changes to "up" or "down".
using TrendColorFn = std::function<void(const String& trend)>;

class PlaceholderEngine {
public:
    PlaceholderEngine();

    // ---- Registration (called by WidgetFactory during XML build) ----

    // Register a label to track a scalar placeholder key.
    // When setValue(key, ...) is called, lv_label_set_text is called on this label.
    void registerLabel(const char* key, lv_obj_t* label);

    // Register a container and rebuild function for an array placeholder.
    // When setArray(key, items) is called, rebuildFn is invoked.
    void registerArray(const char* key, lv_obj_t* container, ArrayRebuildFn rebuildFn);

    // Register an object whose visibility is controlled by a value:
    //   visible when value is non-empty, "true", or "1"; hidden otherwise.
    void registerVisible(const char* key, lv_obj_t* obj);

    // Register a callback for trend coloring (for crypto_row change labels).
    void registerTrend(const char* key, TrendColorFn fn);

    // ---- Value updates ----

    // Set a single scalar value and push to all registered labels.
    void setValue(const char* key, const char* value);

    // Set an array value and trigger all registered array containers.
    void setArray(const char* key, const std::vector<String>& items);

    // Apply a full data.json payload (bulk update).
    void applyData(const std::map<String, String>& scalars,
                   const std::map<String, std::vector<String>>& arrays);

    // Update {time}, {date}, {day}, {weekday}, {month}, {cal_*} from local RTC.
    // tzOffset: hours from UTC (-12..+14).
    void updateRtc(int8_t tzOffset);

    // Update {indoor_temp}, {indoor_hum}, {voc}, {co2} from sensors.
    void updateSensor(float tempC, float humPct, bool ok, float tvoc = 0, float co2 = 0);

    // ---- Resolve ----

    // Replace all {key} tokens in templateStr with current values.
    // Returns the resolved string. Unknown keys are left as {key}.
    String resolve(const char* templateStr) const;

    // Get the current value of a key (empty string if unknown).
    String get(const char* key) const;

    // ---- Lifecycle ----

    // Clear all registrations. Call before rebuilding screens from new XML.
    // Does NOT clear the value store — values survive a screen rebuild.
    void clearRegistrations();

private:
    // Scalar values: key → current string value
    std::map<String, String> _values;
    uint32_t                 _dataFetchedMs = 0;  // millis() when last applyData called
    uint32_t                 _homeMsgGeneratedEpoch = 0; // unix ts (seconds)
    // Keep last valid air-quality readings to avoid UI flicker on transient invalid frames
    uint32_t                 _lastVocValidMs = 0;
    uint32_t                 _lastCo2ValidMs = 0;
    String                   _lastVocValue;
    String                   _lastCo2Value;

    // Scalar label registry: key → list of lv_obj_t* labels
    std::map<String, std::vector<lv_obj_t*>> _labels;

    // Visibility registry: key → list of objects to show/hide
    std::map<String, std::vector<lv_obj_t*>> _visible;

    // Array registry: key → (container, rebuildFn) pairs
    struct ArrayEntry {
        lv_obj_t*      container;
        ArrayRebuildFn rebuildFn;
    };
    std::map<String, std::vector<ArrayEntry>> _arrays;

    // Trend callbacks: key → list of callbacks
    std::map<String, std::vector<TrendColorFn>> _trends;

    void _pushScalar(const String& key, const char* value);
    void _pushArray(const String& key, const std::vector<String>& items);
    void _updateVisibility(const String& key, const char* value);
    void _fireTrend(const String& key, const char* value);
    String _friendlyToLvgl(const String& in) const;
};
