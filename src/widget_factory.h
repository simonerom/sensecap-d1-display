// =============================================================================
// widget_factory.h — XML element → LVGL widget builder
//
// Called by XmlParser for each XML element during layout build.
// All LVGL calls go through lv_helpers.h — no direct lv_* calls here.
// =============================================================================
#pragma once

#include <lvgl.h>
#include <Arduino.h>
#include <map>
#include <functional>
#include "lv_helpers.h"
#include "placeholder_engine.h"
#include "settings_page.h"

// Attribute map type (XML attribute name → value string)
using AttrMap = std::map<String, String>;

class WidgetFactory {
public:
    explicit WidgetFactory(PlaceholderEngine& engine);

    // Set the SettingsPage instance used by buildSettingsForm().
    // Must be called before any buildElement() call that may encounter <settings_form/>.
    void setSettingsPage(SettingsPage* sp) { _settingsPage = sp; }
    void setSettingsCallbacks(std::function<void(const AppSettings&)> onSave, std::function<void(const TouchCalibration&)> onCal);

    // ---- Screen builder ----
    // Creates an lv_scr (lv_obj_create(nullptr)) for the given screen id.
    // bg: parsed from XML bg attribute, default #1A1A2E.
    // Returns the screen object.
    lv_obj_t* buildScreen(const char* id, const AttrMap& attrs);

    // ---- Generic element dispatcher ----
    // parent: LVGL parent object to create the new widget inside.
    // element: XML element name ("card", "label", "row", etc.)
    // attrs: attribute map for this element.
    // Returns the created lv_obj_t* (nullptr for virtual/empty elements).
    lv_obj_t* buildElement(lv_obj_t* parent,
                           const char* element,
                           const AttrMap& attrs);

private:
    PlaceholderEngine& _engine;
    SettingsPage*      _settingsPage = nullptr;
    std::function<void(const AppSettings&)>      _onSettingsSaved;
    std::function<void(const TouchCalibration&)> _onCalDone;

    // ---- Element builders ----
    lv_obj_t* _buildCard(lv_obj_t* parent, const AttrMap& attrs);
    lv_obj_t* _buildLabel(lv_obj_t* parent, const AttrMap& attrs);
    lv_obj_t* _buildRow(lv_obj_t* parent, const AttrMap& attrs);
    lv_obj_t* _buildCol(lv_obj_t* parent, const AttrMap& attrs);
    lv_obj_t* _buildList(lv_obj_t* parent, const AttrMap& attrs);
    lv_obj_t* _buildCryptoRow(lv_obj_t* parent, const AttrMap& attrs);
    lv_obj_t* _buildCalendarGrid(lv_obj_t* parent, const AttrMap& attrs);
    lv_obj_t* _buildEventsList(lv_obj_t* parent, const AttrMap& attrs);
    lv_obj_t* _buildBigClock(lv_obj_t* parent, const AttrMap& attrs);
    lv_obj_t* _buildSettingsForm(lv_obj_t* parent);
    lv_obj_t* _buildHeatingControls(lv_obj_t* parent, const AttrMap& attrs);
    lv_obj_t* _buildCalendarNav(lv_obj_t* parent, const AttrMap& attrs);

    // ---- Helpers ----

    // If text contains {key}, register label with engine and return resolved text.
    // If text is plain, return it unchanged.
    String _resolveAndRegister(lv_obj_t* label, const char* textAttr);

    // Parse width/height string: "100%" → LV_PCT(100), "auto" → LV_SIZE_CONTENT, "120" → 120.
    lv_coord_t _parseDim(const char* val, bool isWidth = true);

    // Parse flex attribute: "1" → treat as flex_grow=1 (child of a row).
    bool _hasFlex(const AttrMap& attrs, uint8_t& grow);

    // Get attribute with fallback.
    String _attr(const AttrMap& attrs, const char* key, const char* def = "") const;
    int    _attrInt(const AttrMap& attrs, const char* key, int def = 0) const;
    bool   _attrBool(const AttrMap& attrs, const char* key, bool def = false) const;
    lv_color_t _attrColor(const AttrMap& attrs, const char* key, uint32_t defHex) const;

    // Apply text alignment (left/center/right) to a label.
    void _applyAlign(lv_obj_t* label, const char* align);
};
