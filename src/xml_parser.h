// =============================================================================
// xml_parser.h — XML layout parser using TinyXML2
//
// Walks the <screens> DOM and calls WidgetFactory for each element.
// The caller provides:
//   - A null-terminated XML string (must remain valid during parse)
//   - A WidgetFactory reference to receive element callbacks
//   - An output array to receive the built screen objects
// =============================================================================
#pragma once

#include <Arduino.h>
#include <lvgl.h>
#include <map>
#include <vector>

// Forward declarations
class WidgetFactory;

// Max number of screens the parser can produce (home, calendar, clock, settings)
#define XML_MAX_SCREENS 4

// Map of screen id → lv_obj_t* produced by parse()
using ScreenMap = std::map<String, lv_obj_t*>;

class XmlParser {
public:
    explicit XmlParser(WidgetFactory& factory);

    // Parse layout XML and build LVGL widget trees.
    // xml: null-terminated XML string (TinyXML2 makes an internal copy — safe to free after).
    // len: length of xml string (0 = auto strlen).
    // screens: output map of id → lv_obj_t*.
    // Returns true on success; false on parse/build error (check lastError()).
    bool parse(const char* xml, size_t len, ScreenMap& screens);

    const String& lastError() const { return _error; }

private:
    WidgetFactory& _factory;
    String         _error;

    // Recursively build a tree of LVGL widgets from an XML element's children.
    // parent: LVGL parent object for the new children.
    // xmlElem: TinyXML2 element pointer (typed as void* to avoid including tinyxml2.h here).
    void _buildChildren(lv_obj_t* parent, void* xmlElem);

    // Build an attribute map from a TinyXML2 element.
    std::map<String, String> _attrMap(void* xmlElem);
};
