// =============================================================================
// xml_parser.cpp — TinyXML2-based layout parser
// =============================================================================
#include "xml_parser.h"
#include "widget_factory.h"
#include <tinyxml2.h>
#include <Arduino.h>

using namespace tinyxml2;

XmlParser::XmlParser(WidgetFactory& factory) : _factory(factory) {}

// =============================================================================
// parse
// =============================================================================
bool XmlParser::parse(const char* xml, size_t len, ScreenMap& screens) {
    _error = "";
    screens.clear();

    if (!xml || (len == 0 && xml[0] == '\0')) {
        _error = "Empty XML";
        return false;
    }

    XMLDocument doc;
    XMLError err = (len > 0) ? doc.Parse(xml, len) : doc.Parse(xml);
    if (err != XML_SUCCESS) {
        _error = String("XML parse error: ") + doc.ErrorStr();
        Serial.println("[XmlParser] " + _error);
        return false;
    }

    // Root element must be <screens>
    XMLElement* root = doc.FirstChildElement("screens");
    if (!root) {
        // Try ignoring XML declaration
        root = doc.RootElement();
        if (!root || strcmp(root->Name(), "screens") != 0) {
            _error = "Root element must be <screens>";
            Serial.println("[XmlParser] " + _error);
            return false;
        }
    }

    // Log version attribute
    const char* ver = root->Attribute("version");
    Serial.printf("[XmlParser] Layout version: %s\n", ver ? ver : "(none)");

    // Iterate <screen> children
    int screenCount = 0;
    for (XMLElement* screenElem = root->FirstChildElement("screen");
         screenElem != nullptr;
         screenElem = screenElem->NextSiblingElement("screen")) {

        const char* id = screenElem->Attribute("id");
        if (!id) {
            Serial.println("[XmlParser] <screen> missing id attribute, skipping.");
            continue;
        }

        Serial.printf("[XmlParser] Building screen: %s\n", id);

        // Build attribute map for the screen itself
        std::map<String, String> screenAttrs = _attrMap(screenElem);

        // Create the LVGL screen object
        lv_obj_t* scr = _factory.buildScreen(id, screenAttrs);
        if (!scr) {
            _error = String("Failed to build screen '") + id + "'";
            return false;
        }

        // Recursively build children
        _buildChildren(scr, screenElem);

        screens[String(id)] = scr;
        screenCount++;
    }

    Serial.printf("[XmlParser] Built %d screens.\n", screenCount);
    return screenCount > 0;
}

// =============================================================================
// _buildChildren — recursively build widget tree
// =============================================================================
void XmlParser::_buildChildren(lv_obj_t* parent, void* xmlElemPtr) {
    XMLElement* elem = static_cast<XMLElement*>(xmlElemPtr);
    if (!elem) return;

    for (XMLElement* child = elem->FirstChildElement();
         child != nullptr;
         child = child->NextSiblingElement()) {

        const char* name = child->Name();
        if (!name) continue;

        std::map<String, String> attrs = _attrMap(child);

        // Build this element (returns the LVGL parent for its children)
        lv_obj_t* newParent = _factory.buildElement(parent, name, attrs);

        // Recurse into children (unless it's a leaf element)
        if (newParent && strcmp(name, "label") != 0 &&
            strcmp(name, "big_clock") != 0 &&
            strcmp(name, "crypto_row") != 0 &&
            strcmp(name, "settings_form") != 0) {
            _buildChildren(newParent, child);
        }
    }
}

// =============================================================================
// _attrMap — extract all attributes of an XML element into a String map
// =============================================================================
std::map<String, String> XmlParser::_attrMap(void* xmlElemPtr) {
    std::map<String, String> result;
    XMLElement* elem = static_cast<XMLElement*>(xmlElemPtr);
    if (!elem) return result;

    for (const XMLAttribute* attr = elem->FirstAttribute();
         attr != nullptr;
         attr = attr->Next()) {
        result[String(attr->Name())] = String(attr->Value());
    }
    return result;
}
