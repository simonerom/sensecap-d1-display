// =============================================================================
// widget_factory.cpp
// =============================================================================
#include "widget_factory.h"
#include <set>
#include "../include/config.h"

WidgetFactory::WidgetFactory(PlaceholderEngine& engine) : _engine(engine) {}

// =============================================================================
// Attribute helpers
// =============================================================================
String WidgetFactory::_attr(const AttrMap& a, const char* k, const char* def) const {
    auto it = a.find(String(k));
    return (it != a.end()) ? it->second : String(def);
}
int WidgetFactory::_attrInt(const AttrMap& a, const char* k, int def) const {
    auto it = a.find(String(k));
    return (it != a.end()) ? it->second.toInt() : def;
}
bool WidgetFactory::_attrBool(const AttrMap& a, const char* k, bool def) const {
    auto it = a.find(String(k));
    if (it == a.end()) return def;
    return it->second == "true" || it->second == "1";
}
lv_color_t WidgetFactory::_attrColor(const AttrMap& a, const char* k, uint32_t defHex) const {
    auto it = a.find(String(k));
    if (it == a.end()) return lv_hlp_hex(defHex);
    return lv_hlp_hex_str(it->second.c_str());
}
lv_coord_t WidgetFactory::_parseDim(const char* val, bool isWidth) {
    if (!val || val[0] == '\0' || strcmp(val, "auto") == 0) return LV_SIZE_CONTENT;
    int len = strlen(val);
    if (val[len-1] == '%') {
        int pct = atoi(val);
        return LV_PCT(pct);
    }
    return (lv_coord_t)atoi(val);
}
bool WidgetFactory::_hasFlex(const AttrMap& attrs, uint8_t& grow) {
    auto it = attrs.find("flex");
    if (it == attrs.end()) return false;
    grow = (uint8_t)it->second.toInt();
    return true;
}
void WidgetFactory::_applyAlign(lv_obj_t* label, const char* align) {
    if (!align) return;
    if (strcmp(align, "center") == 0) {
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(label, LV_PCT(100));
    } else if (strcmp(align, "right") == 0) {
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(label, LV_PCT(100));
    } else {
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_LEFT, 0);
    }
}

// =============================================================================
// _resolveAndRegister
// =============================================================================
String WidgetFactory::_resolveAndRegister(lv_obj_t* label, const char* textAttr) {
    if (!textAttr) return String();
    String resolved = _engine.resolve(textAttr);
    // Find all {key} tokens and register label for live updates
    const char* p = textAttr;
    while (*p) {
        if (*p == '{') {
            const char* end = strchr(p + 1, '}');
            if (end) {
                String key(p + 1, (unsigned)(end - p - 1));
                _engine.registerLabel(key.c_str(), label);
                p = end + 1;
                continue;
            }
        }
        p++;
    }
    return resolved;
}

// =============================================================================
// buildScreen
// =============================================================================
lv_obj_t* WidgetFactory::buildScreen(const char* id, const AttrMap& attrs) {
    lv_obj_t* scr = lv_obj_create(nullptr);
    lv_color_t bg = _attrColor(attrs, "bg", 0x1A1A2E);
    lv_hlp_set_bg(scr, bg);
    lv_hlp_set_border_none(scr);
    lv_hlp_set_radius(scr, 0);
    String gradColorStr = _attr(attrs, "grad_color", "");
    if (gradColorStr.length() > 0) {
        lv_color_t gc = _attrColor(attrs, "grad_color", 0x000000);
        lv_obj_set_style_bg_grad_color(scr, gc, 0);
        lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    }
    int pad = _attrInt(attrs, "pad", 8);
    lv_hlp_set_pad_all(scr, (lv_coord_t)pad);
    // Enable vertical scroll on the screen so content taller than 480px is reachable.
    // Horizontal scroll is disabled — page navigation is handled by ScreenManager gestures.
    lv_obj_add_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(scr, LV_DIR_VER);
    lv_obj_set_style_bg_color(scr, lv_hlp_hex(0x444444), LV_PART_SCROLLBAR);
    lv_obj_set_style_width(scr, 3, LV_PART_SCROLLBAR);
    // Column flex layout for direct screen children
    lv_hlp_flex_col(scr, 8);
    return scr;
}

// =============================================================================
// buildElement — dispatcher
// =============================================================================
lv_obj_t* WidgetFactory::buildElement(lv_obj_t* parent,
                                       const char* element,
                                       const AttrMap& attrs) {
    if (!element) return nullptr;

    if (strcmp(element, "card")          == 0) return _buildCard(parent, attrs);
    if (strcmp(element, "label")         == 0) return _buildLabel(parent, attrs);
    if (strcmp(element, "row")           == 0) return _buildRow(parent, attrs);
    if (strcmp(element, "col")           == 0) return _buildCol(parent, attrs);
    if (strcmp(element, "list")          == 0) return _buildList(parent, attrs);
    if (strcmp(element, "crypto_row")    == 0) return _buildCryptoRow(parent, attrs);
    if (strcmp(element, "calendar_grid") == 0) return _buildCalendarGrid(parent, attrs);
    if (strcmp(element, "events_list")   == 0) return _buildEventsList(parent, attrs);
    if (strcmp(element, "big_clock")     == 0) return _buildBigClock(parent, attrs);
    if (strcmp(element, "settings_form") == 0) return _buildSettingsForm(parent);

    Serial.printf("[WidgetFactory] Unknown element: %s\n", element);
    return nullptr;
}

// =============================================================================
// _buildCard
// =============================================================================
lv_obj_t* WidgetFactory::_buildCard(lv_obj_t* parent, const AttrMap& attrs) {
    lv_color_t bg  = _attrColor(attrs, "bg", 0x16213E);
    int radius     = _attrInt(attrs, "radius", 16);
    int pad        = _attrInt(attrs, "pad", 16);
    int pad_h      = _attrInt(attrs, "pad_h", pad);  // horizontal override
    int pad_v      = _attrInt(attrs, "pad_v", pad);  // vertical override
    bool scroll    = _attrBool(attrs, "scroll", false);
    int gap        = _attrInt(attrs, "gap", 8);
    String wStr    = _attr(attrs, "w", "100%");
    String hStr    = _attr(attrs, "h", "auto");

    bool tight  = _attrBool(attrs, "tight", false);
    String valign = _attr(attrs, "valign", "top");
    lv_obj_t* card = lv_hlp_card(parent, bg, (lv_coord_t)radius, 0);
    lv_obj_set_style_pad_hor(card, (lv_coord_t)pad_h, 0);
    lv_obj_set_style_pad_ver(card, (lv_coord_t)pad_v, 0);
    // bg_opa: glass effect (0-255)
    int bgOpa = _attrInt(attrs, "bg_opa", 255);
    if (bgOpa < 255) lv_obj_set_style_bg_opa(card, (lv_opa_t)bgOpa, 0);
    // border for glass rim
    String borderColorStr = _attr(attrs, "border_color", "");
    int borderW = _attrInt(attrs, "border_width", 0);
    if (borderColorStr.length() > 0 && borderW > 0) {
        lv_color_t bc = _attrColor(attrs, "border_color", 0xFFFFFF);
        lv_obj_set_style_border_color(card, bc, 0);
        lv_obj_set_style_border_width(card, borderW, 0);
        lv_obj_set_style_border_opa(card, LV_OPA_70, 0);
    }
    if (tight) {
        lv_obj_set_style_pad_row(card, 0, 0);
        lv_obj_set_style_pad_gap(card, 0, 0);
    }
    lv_hlp_flex_col(card, (lv_coord_t)gap);
    if (valign == "center") lv_obj_set_style_flex_main_place(card, LV_FLEX_ALIGN_CENTER, 0);
    else if (valign == "bottom") lv_obj_set_style_flex_main_place(card, LV_FLEX_ALIGN_END, 0);

    lv_coord_t w = _parseDim(wStr.c_str(), true);
    lv_coord_t h = _parseDim(hStr.c_str(), false);
    lv_obj_set_width(card, w);
    if (h != LV_SIZE_CONTENT) lv_obj_set_height(card, h);
    else lv_obj_set_height(card, LV_SIZE_CONTENT);

    if (scroll) {
        lv_obj_add_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(card, LV_DIR_VER);
        // Style scrollbar to be thin and subtle
        lv_obj_set_style_bg_color(card, lv_hlp_hex(0x888888), LV_PART_SCROLLBAR);
        lv_obj_set_style_width(card, 3, LV_PART_SCROLLBAR);
    }

    // Flex grow if inside a row
    uint8_t grow;
    if (_hasFlex(attrs, grow)) lv_hlp_flex_grow(card, grow);

    // Visibility controlled by placeholder
    String visAttr = _attr(attrs, "visible", "");
    if (!visAttr.isEmpty() && visAttr.startsWith("{") && visAttr.endsWith("}")) {
        String key = visAttr.substring(1, visAttr.length() - 1);
        _engine.registerVisible(key.c_str(), card);
        // Apply initial visibility based on current engine value
        String cur = _engine.get(key.c_str());
        if (cur == "false" || cur == "0" || cur == "none" || cur.isEmpty()) {
            lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);
        }
    }

    return card;
}

// =============================================================================
// _buildLabel
// =============================================================================
lv_obj_t* WidgetFactory::_buildLabel(lv_obj_t* parent, const AttrMap& attrs) {
    String textAttr = _attr(attrs, "text", "");
    int fontSize    = _attrInt(attrs, "font", 16);
    lv_color_t col  = _attrColor(attrs, "color", 0xFFFFFF);
    String align    = _attr(attrs, "align", "left");
    bool bold       = _attrBool(attrs, "bold", false);
    int maxLines    = _attrInt(attrs, "max_lines", 0);

    lv_obj_t* lbl = lv_label_create(parent);
    lv_hlp_set_text_color(lbl, col);
    lv_hlp_set_font(lbl, lv_hlp_font_ex(fontSize, bold));
    // Zero label's own padding — spacing is controlled by parent gap
    lv_obj_set_style_pad_all(lbl, 0, 0);
    // flex="1" → grow to fill row; otherwise full width for wrapping
    uint8_t flexGrow = 0;
    if (_hasFlex(attrs, flexGrow) && flexGrow > 0) {
        lv_hlp_flex_grow(lbl, flexGrow);
    } else {
        lv_obj_set_width(lbl, LV_PCT(100));
    }

    if (maxLines > 0) {
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    } else {
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
    }

    // Resolve text, register for live updates if contains {placeholder}
    String resolved = _resolveAndRegister(lbl, textAttr.c_str());
    lv_label_set_text(lbl, resolved.c_str());

    _applyAlign(lbl, align.c_str());

    return lbl;
}

// =============================================================================
// _buildRow
// =============================================================================
lv_obj_t* WidgetFactory::_buildRow(lv_obj_t* parent, const AttrMap& attrs) {
    int gap  = _attrInt(attrs, "gap", 8);
    int pad  = _attrInt(attrs, "pad", 0);

    lv_obj_t* row = lv_hlp_obj(parent);
    lv_hlp_flex_row(row, (lv_coord_t)gap);
    lv_obj_set_width(row, LV_PCT(100));
    String rowH = _attr(attrs, "h", "auto");
    lv_coord_t rowHVal = _parseDim(rowH.c_str(), false);
    if (rowHVal != LV_SIZE_CONTENT) lv_obj_set_height(row, rowHVal);
    else lv_obj_set_height(row, LV_SIZE_CONTENT);
    // stretch children vertically by default
    lv_obj_set_style_flex_cross_place(row, LV_FLEX_ALIGN_START, 0);
    if (pad > 0) lv_hlp_set_pad_all(row, (lv_coord_t)pad);

    // Align children vertically
    String align = _attr(attrs, "align", "stretch");
    lv_flex_align_t cross = LV_FLEX_ALIGN_START;
    if (align == "center") cross = LV_FLEX_ALIGN_CENTER;
    else if (align == "bottom") cross = LV_FLEX_ALIGN_END;
    lv_obj_set_style_flex_cross_place(row, cross, 0);

    return row;
}


// =============================================================================
// _buildCol — transparent vertical flex container
// =============================================================================
lv_obj_t* WidgetFactory::_buildCol(lv_obj_t* parent, const AttrMap& attrs) {
    int gap = _attrInt(attrs, "gap", 8);
    int pad = _attrInt(attrs, "pad", 0);

    lv_obj_t* col = lv_hlp_obj(parent);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_hlp_flex_col(col, (lv_coord_t)gap);
    lv_obj_set_width(col, LV_SIZE_CONTENT);
    lv_obj_set_height(col, LV_PCT(100));
    if (pad > 0) lv_hlp_set_pad_all(col, (lv_coord_t)pad);

    // flex grow within parent row
    uint8_t grow;
    if (_hasFlex(attrs, grow)) lv_hlp_flex_grow(col, grow);

    return col;
}

// =============================================================================
// _buildList — static items container; array placeholder wires a rebuild fn
// =============================================================================
lv_obj_t* WidgetFactory::_buildList(lv_obj_t* parent, const AttrMap& attrs) {
    String itemsAttr  = _attr(attrs, "items", "");
    int fontSize      = _attrInt(attrs, "font", 14);
    lv_color_t col    = _attrColor(attrs, "color", 0xCCCCCC);
    lv_color_t divCol = _attrColor(attrs, "divider", 0x000000);
    bool hasDivider   = attrs.count("divider") > 0;
    String bullet     = _attr(attrs, "bullet", "• ");

    // Container for the list items
    lv_obj_t* cont = lv_hlp_obj(parent);
    lv_hlp_flex_col(cont, 4);
    lv_obj_set_width(cont, LV_PCT(100));
    lv_obj_set_height(cont, LV_SIZE_CONTENT);

    // Store config in a heap struct for the rebuild lambda
    struct ListCfg {
        int fontSize; lv_color_t col; lv_color_t divCol; bool hasDivider; String bullet;
    };
    auto* cfg = new ListCfg{fontSize, col, divCol, hasDivider, bullet};

    // Wire up array placeholder
    if (!itemsAttr.isEmpty() && itemsAttr.startsWith("{") && itemsAttr.endsWith("}")) {
        String key = itemsAttr.substring(1, itemsAttr.length() - 1);
        _engine.registerArray(key.c_str(), cont,
            [cfg](lv_obj_t* c, const std::vector<String>& items) {
                // Remove old children
                lv_obj_clean(c);
                for (size_t i = 0; i < items.size(); i++) {
                    // Optional divider line before each item (except the first)
                    if (cfg->hasDivider && i > 0) {
                        lv_obj_t* div = lv_obj_create(c);
                        lv_obj_set_size(div, LV_PCT(100), 1);
                        lv_hlp_set_bg(div, cfg->divCol);
                        lv_hlp_set_border_none(div);
                        lv_hlp_set_radius(div, 0);
                        lv_hlp_set_pad_all(div, 0);
                    }
                    String text = cfg->bullet + items[i];
                    lv_obj_t* lbl = lv_label_create(c);
                    lv_label_set_text(lbl, text.c_str());
                    lv_hlp_set_text_color(lbl, cfg->col);
                    lv_hlp_set_font(lbl, lv_hlp_font(cfg->fontSize));
                    lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
                    lv_obj_set_width(lbl, LV_PCT(100));
                }
            }
        );
    }

    return cont;
}

// =============================================================================
// _buildCryptoRow — symbol | price | change (colored by trend)
// =============================================================================
lv_obj_t* WidgetFactory::_buildCryptoRow(lv_obj_t* parent, const AttrMap& attrs) {
    String symbolAttr = _attr(attrs, "symbol", "BTC");
    String priceAttr  = _attr(attrs, "price",  "--");
    String changeAttr = _attr(attrs, "change", "--");
    String trendAttr  = _attr(attrs, "trend",  "up");
    lv_color_t upCol  = _attrColor(attrs, "up_color",   0x00D4AA);
    lv_color_t dnCol  = _attrColor(attrs, "down_color", 0xFF6B6B);

    lv_obj_t* row = lv_hlp_obj(parent);
    lv_hlp_flex_row(row, 8);
    lv_obj_set_width(row, LV_PCT(100));
    String rowH = _attr(attrs, "h", "auto");
    lv_coord_t rowHVal = _parseDim(rowH.c_str(), false);
    if (rowHVal != LV_SIZE_CONTENT) lv_obj_set_height(row, rowHVal);
    else lv_obj_set_height(row, LV_SIZE_CONTENT);
    // stretch children vertically by default
    lv_obj_set_style_flex_cross_place(row, LV_FLEX_ALIGN_START, 0);
    lv_obj_set_style_pad_top(row, 4, 0);
    lv_obj_set_style_pad_bottom(row, 4, 0);

    // Symbol label
    lv_obj_t* symLbl = lv_label_create(row);
    lv_hlp_set_font(symLbl, lv_hlp_font_bold(18));
    lv_hlp_set_text_color(symLbl, lv_hlp_hex(0x1A1A2E));
    lv_obj_set_width(symLbl, 70);
    String symRes = _resolveAndRegister(symLbl, symbolAttr.c_str());
    lv_label_set_text(symLbl, symRes.c_str());

    // Price label (flex grow to fill remaining space)
    lv_obj_t* priceLbl = lv_label_create(row);
    lv_hlp_set_font(priceLbl, lv_hlp_font(18));
    lv_hlp_set_text_color(priceLbl, lv_hlp_hex(0x333333));
    lv_hlp_flex_grow(priceLbl, 1);
    String priceRes = _resolveAndRegister(priceLbl, priceAttr.c_str());
    lv_label_set_text(priceLbl, priceRes.c_str());

    // Change label (trend-colored)
    lv_obj_t* changeLbl = lv_label_create(row);
    lv_hlp_set_font(changeLbl, lv_hlp_font(14));
    lv_obj_set_width(changeLbl, 70);
    lv_obj_set_style_text_align(changeLbl, LV_TEXT_ALIGN_RIGHT, 0);

    // Initial trend coloring
    String curTrend = _engine.resolve(trendAttr.c_str());
    lv_hlp_set_text_color(changeLbl, curTrend == "up" ? upCol : dnCol);

    String changeRes = _resolveAndRegister(changeLbl, changeAttr.c_str());
    lv_label_set_text(changeLbl, changeRes.c_str());

    // Register trend key for live recoloring
    if (trendAttr.startsWith("{") && trendAttr.endsWith("}")) {
        String trendKey = trendAttr.substring(1, trendAttr.length() - 1);
        _engine.registerTrend(trendKey.c_str(),
            [changeLbl, upCol, dnCol](const String& t) {
                lv_hlp_set_text_color(changeLbl, t == "up" ? upCol : dnCol);
            }
        );
    }

    return row;
}

// =============================================================================
// _buildCalendarGrid — redesigned: white card, big cells, today circle, event dots
// =============================================================================
lv_obj_t* WidgetFactory::_buildCalendarGrid(lv_obj_t* parent, const AttrMap& attrs) {
    String yearAttr     = _attr(attrs, "year",        "{cal_year}");
    String monthAttr    = _attr(attrs, "month",       "{cal_month}");
    String todayAttr    = _attr(attrs, "today",       "{cal_today}");
    String evDaysAttr   = _attr(attrs, "event_days",  "{event_days}");
    lv_color_t hlCol    = _attrColor(attrs, "highlight_color", 0xD63384);  // fuchsia today
    lv_color_t txtCol   = _attrColor(attrs, "text_color",      0x1A1A2E);
    lv_color_t hdrCol   = _attrColor(attrs, "header_color",    0x888888);
    lv_color_t dotCol   = _attrColor(attrs, "dot_color",       0x5B21B6);
    lv_color_t cellBg   = _attrColor(attrs, "cell_bg",         0xEFEFEF);
    String holDaysAttr  = _attr(attrs, "holiday_days", "{holiday_days}");

    // Italian day headers Mon-first
    static const char* dayNames[] = { "Lu","Ma","Me","Gi","Ve","Sa","Do" };

    const int CELL_W = 66;
    const int CELL_H = 50;

    // Outer white card
    lv_obj_t* card = lv_hlp_card(parent, lv_hlp_hex(0xFFFFFF), 12, 0);
    lv_obj_set_width(card, LV_PCT(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_hlp_flex_col(card, 4);

    // Day-of-week header row
    lv_obj_t* hdr = lv_hlp_obj(card);
    lv_obj_set_width(hdr, LV_PCT(100));
    lv_obj_set_height(hdr, 24);
    lv_hlp_flex_row(hdr, 0);
    for (int d = 0; d < 7; d++) {
        lv_obj_t* lbl = lv_label_create(hdr);
        lv_label_set_text(lbl, dayNames[d]);
        lv_hlp_set_font(lbl, lv_hlp_font(13));
        lv_hlp_set_text_color(lbl, hdrCol);
        lv_obj_set_width(lbl, CELL_W);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    }

    // 42 day cells
    lv_obj_t* grid = lv_hlp_obj(card);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_height(grid, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, 2, 0);
    lv_obj_set_style_pad_column(grid, 0, 0);

    lv_obj_t** cells = (lv_obj_t**)malloc(42 * sizeof(lv_obj_t*));
    for (int i = 0; i < 42; i++) {
        lv_obj_t* cell = lv_obj_create(grid);
        lv_obj_set_size(cell, CELL_W, CELL_H);
        lv_hlp_set_bg(cell, cellBg);
        lv_hlp_set_border_none(cell);
        lv_hlp_set_radius(cell, 8);
        lv_hlp_no_scroll(cell);
        lv_hlp_set_pad_all(cell, 4);
        lv_hlp_flex_col(cell, 1);
        lv_obj_set_style_flex_main_place(cell, LV_FLEX_ALIGN_CENTER, 0);
        lv_obj_set_style_flex_cross_place(cell, LV_FLEX_ALIGN_CENTER, 0);

        // Day number label
        lv_obj_t* lbl = lv_label_create(cell);
        lv_label_set_text(lbl, "");
        lv_hlp_set_font(lbl, lv_hlp_font(14));
        lv_hlp_set_text_color(lbl, txtCol);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);

        // Event dot (hidden by default)
        lv_obj_t* dot = lv_obj_create(cell);
        lv_obj_set_size(dot, 18, 3);
        lv_hlp_set_bg(dot, dotCol);
        lv_hlp_set_border_none(dot);
        lv_hlp_set_radius(dot, 2);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);

        cells[i] = cell;
    }

    struct CalState { int year=0, month=0, today=0; std::set<int> evDays; std::set<int> holDays; };
    auto* state = new CalState();
    lv_obj_t** capturedCells = cells;
    lv_color_t capturedHl  = hlCol;
    lv_color_t capturedTxt = txtCol;
    lv_color_t capturedDot = dotCol;

    lv_color_t capturedCellBg = cellBg;
    auto rebuild = [capturedCells, state, capturedHl, capturedTxt, capturedDot, capturedCellBg]() {
        int y = state->year, m = state->month, today = state->today;
        if (y < 2020 || m < 1 || m > 12) return;

        struct tm t = {};
        t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = 1;
        mktime(&t);
        int startDow = (t.tm_wday + 6) % 7; // 0=Mon

        int daysInMonth = 31;
        if (m==4||m==6||m==9||m==11) daysInMonth=30;
        else if (m==2) daysInMonth=((y%4==0&&y%100!=0)||y%400==0)?29:28;

        for (int i = 0; i < 42; i++) {
            int day = i - startDow + 1;
            lv_obj_t* cell = capturedCells[i];
            lv_obj_t* lbl  = lv_obj_get_child(cell, 0);
            lv_obj_t* dot  = lv_obj_get_child(cell, 1);
            if (!lbl) continue;

            if (day < 1 || day > daysInMonth) {
                lv_label_set_text(lbl, "");
                lv_hlp_set_bg(cell, capturedCellBg, LV_OPA_TRANSP);
                if (dot) lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
            } else {
                char buf[4];
                snprintf(buf, sizeof(buf), "%d", day);
                lv_label_set_text(lbl, buf);
                bool isToday = (day == today);
                bool isHol   = state->holDays.count(day) > 0;
                if (isToday) {
                    lv_hlp_set_bg(cell, capturedHl);
                    lv_hlp_set_text_color(lbl, lv_color_white());
                } else {
                    lv_hlp_set_bg(cell, capturedCellBg);
                    lv_color_t tc = isHol ? lv_hlp_hex(0xD32F2F) : capturedTxt;
                    lv_hlp_set_text_color(lbl, tc);
                }
                // Event dot
                if (dot) {
                    bool hasEv = state->evDays.count(day) > 0;
                    if (hasEv && !isToday) {
                        lv_hlp_set_bg(dot, capturedDot);
                        lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
                    } else if (hasEv && isToday) {
                        lv_hlp_set_bg(dot, lv_color_white());
                        lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
                    }
                }
            }
        }
    };

    // Register placeholders
    auto reg = [&](const String& attr, int* field) {
        if (attr.startsWith("{") && attr.endsWith("}")) {
            String key = attr.substring(1, attr.length()-1);
            _engine.registerTrend(key.c_str(), [state, rebuild, field](const String& v) {
                *field = v.toInt(); rebuild();
            });
            *field = _engine.get(key.c_str()).toInt();
        } else { *field = attr.toInt(); }
    };
    reg(yearAttr,  &state->year);
    reg(monthAttr, &state->month);
    reg(todayAttr, &state->today);

    // holiday_days
    auto parseIntSet = [](const String& v, std::set<int>& s) {
        s.clear();
        int start = 0;
        while (start < (int)v.length()) {
            int comma = v.indexOf(',', start);
            if (comma < 0) comma = v.length();
            int d = v.substring(start, comma).toInt();
            if (d > 0) s.insert(d);
            start = comma + 1;
        }
    };
    if (holDaysAttr.startsWith("{") && holDaysAttr.endsWith("}")) {
        String key = holDaysAttr.substring(1, holDaysAttr.length()-1);
        _engine.registerTrend(key.c_str(), [state, rebuild, parseIntSet](const String& v) {
            parseIntSet(v, state->holDays); rebuild();
        });
        parseIntSet(_engine.get(key.c_str()), state->holDays);
    }

    // event_days: comma-separated day numbers, e.g. "3,9,10,25"
    if (evDaysAttr.startsWith("{") && evDaysAttr.endsWith("}")) {
        String key = evDaysAttr.substring(1, evDaysAttr.length()-1);
        auto parseEvDays = [state, rebuild, parseIntSet](const String& v) {
            parseIntSet(v, state->evDays); rebuild();
        };
        _engine.registerTrend(key.c_str(), [parseEvDays](const String& v) { parseEvDays(v); });
        parseEvDays(_engine.get(key.c_str()));
    }

    rebuild();
    return card;
}
// =============================================================================
// _buildEventsList
// =============================================================================
lv_obj_t* WidgetFactory::_buildEventsList(lv_obj_t* parent, const AttrMap& attrs) {
    String itemsAttr = _attr(attrs, "items",      "{events}");
    int fontSize     = _attrInt(attrs, "font",     15);
    lv_color_t col   = _attrColor(attrs, "color",  0xFFFFFF);
    lv_color_t dtCol = _attrColor(attrs, "date_color", 0x00D4AA);

    lv_obj_t* cont = lv_hlp_obj(parent);
    lv_hlp_flex_col(cont, 6);
    lv_obj_set_width(cont, LV_PCT(100));
    lv_obj_set_height(cont, LV_SIZE_CONTENT);

    // Events come pre-formatted from data_fetcher.cpp as "HH:MM  d mon  Title"
    // We display each event as two lines: date/time header + title.
    struct EvCfg { int fontSize; lv_color_t col; lv_color_t dtCol; };
    auto* cfg = new EvCfg{fontSize, col, dtCol};

    if (!itemsAttr.isEmpty() && itemsAttr.startsWith("{") && itemsAttr.endsWith("}")) {
        String key = itemsAttr.substring(1, itemsAttr.length() - 1);
        _engine.registerArray(key.c_str(), cont,
            [cfg](lv_obj_t* c, const std::vector<String>& items) {
                lv_obj_clean(c);
                for (const String& line : items) {
                    // Format: "TITLE|||Marzo 15, 13:00 - 13:30"
                    int sep = line.indexOf("|||");
                    String title   = (sep > 0) ? line.substring(0, sep)   : line;
                    String dateStr = (sep > 0) ? line.substring(sep + 3)  : "";

                    lv_obj_t* row = lv_hlp_obj(c);
                    lv_hlp_flex_col(row, 3);
                    lv_obj_set_width(row, LV_PCT(100));
                    lv_obj_set_height(row, LV_SIZE_CONTENT);
                    lv_obj_set_style_pad_bottom(row, 6, 0);

                    // Line 1: title bold
                    lv_obj_t* titleLbl = lv_label_create(row);
                    lv_label_set_text(titleLbl, title.c_str());
                    lv_hlp_set_font(titleLbl, lv_hlp_font(cfg->fontSize));
                    lv_obj_set_style_text_font(titleLbl, lv_hlp_font_bold(cfg->fontSize), 0);
                    lv_hlp_set_text_color(titleLbl, cfg->col);
                    lv_label_set_long_mode(titleLbl, LV_LABEL_LONG_WRAP);
                    lv_obj_set_width(titleLbl, LV_PCT(100));

                    // Line 2: clock icon + date/time in grey
                    if (!dateStr.isEmpty()) {
                        String dtFull = String("ï ") + dateStr; // clock glyph
                        lv_obj_t* dtLbl = lv_label_create(row);
                        lv_label_set_text(dtLbl, (String(LV_SYMBOL_DUMMY) + dateStr).c_str());
                        // Use plain text with clock symbol
                        char dtBuf[128];
                        snprintf(dtBuf, sizeof(dtBuf), "@ %s", dateStr.c_str());
                        lv_label_set_text(dtLbl, dtBuf);
                        lv_hlp_set_font(dtLbl, lv_hlp_font(cfg->fontSize - 2));
                        lv_hlp_set_text_color(dtLbl, cfg->dtCol);
                        lv_obj_set_width(dtLbl, LV_PCT(100));
                    }

                    // Divider line
                    lv_obj_t* div = lv_obj_create(c);
                    lv_obj_set_size(div, LV_PCT(100), 1);
                    lv_hlp_set_bg(div, lv_hlp_hex(0xDDDDDD));
                    lv_hlp_set_border_none(div);
                    lv_hlp_set_radius(div, 0);
                }
            }
        );
    }
    return cont;
}

// =============================================================================
// _buildBigClock
// =============================================================================
lv_obj_t* WidgetFactory::_buildBigClock(lv_obj_t* parent, const AttrMap& attrs) {
    int fontSize   = _attrInt(attrs, "font", 48);  // use 48 (largest compiled-in)
    lv_color_t col = _attrColor(attrs, "color", 0x1A1A2E);
    String align   = _attr(attrs, "align", "center");
    String format  = _attr(attrs, "format", "HH:MM");

    // Decide which placeholder to use based on format
    const char* placeholder = (format == "HH:MM:SS") ? "{time_seconds}" : "{time}";

    lv_obj_t* lbl = lv_label_create(parent);
    lv_hlp_set_font(lbl, lv_hlp_font(fontSize));
    lv_hlp_set_text_color(lbl, col);

    String resolved = _resolveAndRegister(lbl, placeholder);
    lv_label_set_text(lbl, resolved.isEmpty() ? "--:--" : resolved.c_str());
    _applyAlign(lbl, align.c_str());

    return lbl;
}

// =============================================================================

void WidgetFactory::setSettingsCallbacks(
    std::function<void(const AppSettings&)> onSave,
    std::function<void(const TouchCalibration&)> onCal)
{
    _onSettingsSaved = onSave;
    _onCalDone = onCal;
}
// _buildSettingsForm
// =============================================================================
lv_obj_t* WidgetFactory::_buildSettingsForm(lv_obj_t* parent) {
    if (_settingsPage) {
        // Build the settings form into parent using stored callbacks
        _settingsPage->build(parent, _onSettingsSaved, _onCalDone);
        return parent;
    }
    // Fallback: empty placeholder
    lv_obj_t* lbl = lv_hlp_label(parent, "[settings_form not configured]",
                                   lv_hlp_hex(0xFF4444), 14);
    return lbl;
}
