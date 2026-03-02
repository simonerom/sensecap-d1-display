#include "ui.h"
#include "../include/config.h"

#include <Wire.h>
#include <PCA95x5.h>
// Include only the needed Arduino_GFX components to avoid pulling in
// Arduino_ESP32SPI.h which requires esp32-hal-periman.h (ESP32 Arduino 3.x)
#include <Arduino_DataBus.h>
#include <databus/Arduino_ESP32RGBPanel.h>
#include <Arduino_GFX.h>
#include <display/Arduino_RGB_Display.h>
#include <lvgl.h>

// =============================================================================
// SenseCAP Indicator D1 Pro — Arduino_GFX RGB panel driver
//
// Display: ST7701S, 480x480, RGB interface
// SPI init bus (Mode 3, 9-bit): MOSI=48, SCK=41
// CS=PCA9535 P04, RST=PCA9535 P05, I2C expander SDA=39, SCL=40
// RGB data bus: R[4:0]=0-4, G[5:0]=5-10, B[4:0]=11-15
// RGB control: DE=18, VSYNC=17, HSYNC=16, PCLK=21
// Backlight: GPIO 45
//
// Touch: FT5x06, I2C 0x38, same bus SDA=39 SCL=40
// Touch RST=PCA9535 P07, INT=PCA9535 P06 (polled, not used)
// =============================================================================

#define LCD_SCK_PIN  41
#define LCD_MOSI_PIN 48

// PCA9535 I2C expander (CS=P04, RST=P05)
static PCA9535 pca;

// RGB panel (no SPI bus object — we bit-bang init manually)
static Arduino_ESP32RGBPanel *rgbpanel = nullptr;

// Top-level GFX object
static Arduino_RGB_Display *gfx = nullptr;

static const uint32_t LV_BUF_SIZE = SCREEN_WIDTH * 20;
static lv_color_t lv_buf1[LV_BUF_SIZE];
static lv_color_t lv_buf2[LV_BUF_SIZE];
static lv_disp_draw_buf_t lv_draw_buf;
static lv_disp_drv_t lv_disp_drv;

// =============================================================================
// LVGL display flush callback
// =============================================================================
static void disp_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_map) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)color_map, w, h);
    lv_disp_flush_ready(drv);
}

// =============================================================================
// LVGL tick ISR
// =============================================================================
static void IRAM_ATTR lvgl_tick_isr(void* arg) {
    lv_tick_inc(5);
}

// =============================================================================
// ST7701S 9-bit SPI Mode 3 bit-bang
// Mode 3: SCK idle HIGH, data clocked on falling edge (CPOL=1, CPHA=1)
// 9-bit frame: D/C bit (0=cmd, 1=data) followed by 8 data bits, MSB first
// CS is controlled externally via PCA9535 P04
// =============================================================================
static inline void spi_write_bit(bool bit) {
    // SCK is HIGH (idle). Set MOSI, then fall SCK to latch, then restore HIGH.
    if (bit) {
        digitalWrite(LCD_MOSI_PIN, HIGH);
    } else {
        digitalWrite(LCD_MOSI_PIN, LOW);
    }
    digitalWrite(LCD_SCK_PIN, LOW);   // falling edge — ST7701S latches here
    digitalWrite(LCD_SCK_PIN, HIGH);  // return to idle
}

static void lcd_spi_write_cmd(uint8_t cmd) {
    spi_write_bit(0); // D/C = 0 for command
    for (int i = 7; i >= 0; i--) {
        spi_write_bit((cmd >> i) & 1);
    }
}

static void lcd_spi_write_data(uint8_t data) {
    spi_write_bit(1); // D/C = 1 for data
    for (int i = 7; i >= 0; i--) {
        spi_write_bit((data >> i) & 1);
    }
}

// CS low (assert), transfer, CS high (deassert)
static void lcd_cmd(uint8_t cmd) {
    pca.write(PCA95x5::Port::P04, PCA95x5::Level::L);
    lcd_spi_write_cmd(cmd);
    pca.write(PCA95x5::Port::P04, PCA95x5::Level::H);
}

static void lcd_cmd_data(uint8_t cmd, const uint8_t *data, size_t len) {
    pca.write(PCA95x5::Port::P04, PCA95x5::Level::L);
    lcd_spi_write_cmd(cmd);
    for (size_t i = 0; i < len; i++) {
        lcd_spi_write_data(data[i]);
    }
    pca.write(PCA95x5::Port::P04, PCA95x5::Level::H);
}

// Helper macros for single/double data-byte writes
#define LCD_CMD(c)         lcd_cmd(c)
#define LCD_CMD1(c, d0)    do { uint8_t _d[]={d0}; lcd_cmd_data(c,_d,1); } while(0)
#define LCD_CMD2(c,d0,d1)  do { uint8_t _d[]={d0,d1}; lcd_cmd_data(c,_d,2); } while(0)

// =============================================================================
// ST7701S init sequence for SenseCAP Indicator D1 Pro
// Based on st7701_type1 from Arduino_GFX + ESPHome working config
// =============================================================================
static void lcd_init_sequence() {
    // --- Bank 0 ---
    { uint8_t d[] = {0x77, 0x01, 0x00, 0x00, 0x10}; lcd_cmd_data(0xFF, d, 5); }

    LCD_CMD2(0xC0, 0x3B, 0x00);
    LCD_CMD2(0xC1, 0x0D, 0x02);
    LCD_CMD2(0xC2, 0x31, 0x05);
    LCD_CMD1(0xCD, 0x08);

    { uint8_t d[] = {0x00,0x11,0x18,0x0E,0x11,0x06,0x07,0x08,
                     0x07,0x22,0x04,0x12,0x0F,0xAA,0x31,0x18};
      lcd_cmd_data(0xB0, d, 16); }  // Positive Gamma

    { uint8_t d[] = {0x00,0x11,0x19,0x0E,0x12,0x07,0x08,0x08,
                     0x08,0x22,0x04,0x11,0x11,0xA9,0x32,0x18};
      lcd_cmd_data(0xB1, d, 16); }  // Negative Gamma

    // --- Bank 1 ---
    { uint8_t d[] = {0x77, 0x01, 0x00, 0x00, 0x11}; lcd_cmd_data(0xFF, d, 5); }

    LCD_CMD1(0xB0, 0x60);  // Vop=4.7375V
    LCD_CMD1(0xB1, 0x32);  // VCOM
    LCD_CMD1(0xB2, 0x07);  // VGH=15V
    LCD_CMD1(0xB3, 0x80);
    LCD_CMD1(0xB5, 0x49);  // VGL=-10.17V
    LCD_CMD1(0xB7, 0x85);
    LCD_CMD1(0xB8, 0x21);  // AVDD=6.6 & AVCL=-4.6
    LCD_CMD1(0xC1, 0x78);
    LCD_CMD1(0xC2, 0x78);

    { uint8_t d[] = {0x00, 0x1B, 0x02}; lcd_cmd_data(0xE0, d, 3); }

    { uint8_t d[] = {0x08,0xA0,0x00,0x00,0x07,0xA0,0x00,0x00,0x00,0x44,0x44};
      lcd_cmd_data(0xE1, d, 11); }

    { uint8_t d[] = {0x11,0x11,0x44,0x44,0xED,0xA0,0x00,0x00,0xEC,0xA0,0x00,0x00};
      lcd_cmd_data(0xE2, d, 12); }

    { uint8_t d[] = {0x00, 0x00, 0x11, 0x11}; lcd_cmd_data(0xE3, d, 4); }
    LCD_CMD2(0xE4, 0x44, 0x44);

    { uint8_t d[] = {0x0A,0xE9,0xD8,0xA0,0x0C,0xEB,0xD8,0xA0,
                     0x0E,0xED,0xD8,0xA0,0x10,0xEF,0xD8,0xA0};
      lcd_cmd_data(0xE5, d, 16); }

    { uint8_t d[] = {0x00, 0x00, 0x11, 0x11}; lcd_cmd_data(0xE6, d, 4); }
    LCD_CMD2(0xE7, 0x44, 0x44);

    { uint8_t d[] = {0x09,0xE8,0xD8,0xA0,0x0B,0xEA,0xD8,0xA0,
                     0x0D,0xEC,0xD8,0xA0,0x0F,0xEE,0xD8,0xA0};
      lcd_cmd_data(0xE8, d, 16); }

    { uint8_t d[] = {0x02,0x00,0xE4,0xE4,0x88,0x00,0x40};
      lcd_cmd_data(0xEB, d, 7); }

    LCD_CMD2(0xEC, 0x3C, 0x00);

    { uint8_t d[] = {0xAB,0x89,0x76,0x54,0x02,0xFF,0xFF,0xFF,
                     0xFF,0xFF,0xFF,0x20,0x45,0x67,0x98,0xBA};
      lcd_cmd_data(0xED, d, 16); }

    // --- VAP & VAN ---
    { uint8_t d[] = {0x77, 0x01, 0x00, 0x00, 0x13}; lcd_cmd_data(0xFF, d, 5); }
    LCD_CMD1(0xE5, 0xE4);

    // --- Bank 0, display on ---
    { uint8_t d[] = {0x77, 0x01, 0x00, 0x00, 0x00}; lcd_cmd_data(0xFF, d, 5); }

    // Sunlight readable enhancement (from ESPHome working config)
    LCD_CMD1(0xE0, 0x1F);

    LCD_CMD(0x21);          // Display Inversion ON (IPS panel)
    LCD_CMD1(0x3A, 0x60);   // Pixel format: RGB666

    LCD_CMD(0x11);          // Sleep Out
    delay(120);

    LCD_CMD(0x29);          // Display ON
    delay(20);
}

// =============================================================================
// Display initialization
// =============================================================================
void lvgl_display_init() {
    // I2C bus for PCA9535 (SDA=39, SCL=40)
    Wire.begin(39, 40, 400000UL);

    // Initialize PCA9535 and configure CS (P04) and RST (P05) as outputs
    pca.attach(Wire, 0x20);
    pca.polarity(PCA95x5::Polarity::ORIGINAL_ALL);
    pca.direction(PCA95x5::Port::P04, PCA95x5::Direction::OUT);
    pca.direction(PCA95x5::Port::P05, PCA95x5::Direction::OUT);

    // SPI pins: SCK idle HIGH (Mode 3), MOSI idle LOW
    pinMode(LCD_SCK_PIN,  OUTPUT);
    pinMode(LCD_MOSI_PIN, OUTPUT);
    digitalWrite(LCD_SCK_PIN,  HIGH);  // Mode 3: SCK idles HIGH
    digitalWrite(LCD_MOSI_PIN, LOW);

    // CS deasserted (HIGH) and RST deasserted (HIGH) initially
    pca.write(PCA95x5::Port::P04, PCA95x5::Level::H);
    pca.write(PCA95x5::Port::P05, PCA95x5::Level::H);

    // Hardware reset: RST low → high
    pca.write(PCA95x5::Port::P05, PCA95x5::Level::L);
    delay(10);
    pca.write(PCA95x5::Port::P05, PCA95x5::Level::H);
    delay(120);  // ST7701S needs 120ms after reset before init

    // Run ST7701S SPI init sequence (Mode 3, 9-bit)
    lcd_init_sequence();

    // RGB panel with confirmed ST7701S timing (from Arduino_GFX discussion #334)
    rgbpanel = new Arduino_ESP32RGBPanel(
        18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
        4,  3,  2,  1,  0,       /* R4..R0 */
        10, 9,  8,  7,  6,  5,   /* G5..G0 */
        15, 14, 13, 12, 11,      /* B4..B0 */
        1  /* hsync_polarity */,
        10 /* hsync_front_porch */,
        8  /* hsync_pulse_width */,
        50 /* hsync_back_porch */,
        1  /* vsync_polarity */,
        10 /* vsync_front_porch */,
        8  /* vsync_pulse_width */,
        20 /* vsync_back_porch */);

    // No SPI bus passed — init already done above
    gfx = new Arduino_RGB_Display(
        SCREEN_WIDTH, SCREEN_HEIGHT,
        rgbpanel,
        DISPLAY_ROTATION,
        true /* auto_flush */);

    gfx->begin();
    gfx->fillScreen(BLACK);

    // Backlight on GPIO 45
    pinMode(GFX_BL, OUTPUT);
    ledcSetup(0, 5000, 8);
    ledcAttachPin(GFX_BL, 0);
    ledcWrite(0, BACKLIGHT_BRIGHTNESS);

    // LVGL init and display driver registration
    lv_init();

    lv_disp_draw_buf_init(&lv_draw_buf, lv_buf1, lv_buf2, LV_BUF_SIZE);

    lv_disp_drv_init(&lv_disp_drv);
    lv_disp_drv.hor_res  = SCREEN_WIDTH;
    lv_disp_drv.ver_res  = SCREEN_HEIGHT;
    lv_disp_drv.flush_cb = disp_flush_cb;
    lv_disp_drv.draw_buf = &lv_draw_buf;
    lv_disp_drv_register(&lv_disp_drv);
}

// =============================================================================
// FT5x06 touch driver + LVGL indev
// Chip: FT5x06 (Focal Tech), I2C 0x38, bus SDA=39 SCL=40 (shared with PCA9535)
// RST: PCA9535 P07 — toggled during init
// Coordinates: 12-bit, polled. With display ROTATION=2 (180°):
//   x_lvgl = (SCREEN_WIDTH  - 1) - x_raw
//   y_lvgl = (SCREEN_HEIGHT - 1) - y_raw
// =============================================================================
#define FT5X06_ADDR          0x48  // FT6336U (GX screen variant, confirmed by I2C scan)
#define FT5X06_REG_MODE      0x00
#define FT5X06_REG_NUMTOUCH  0x02
#define FT5X06_REG_TOUCH1_XH 0x03  // [7:6]=event [3:0]=Xhigh
#define FT5X06_REG_TOUCH1_XL 0x04  // Xlow
#define FT5X06_REG_TOUCH1_YH 0x05  // [7:4]=touchID [3:0]=Yhigh
#define FT5X06_REG_TOUCH1_YL 0x06  // Ylow
#define FT5X06_REG_THGROUP   0x80

static uint8_t ft5x06_read_reg(uint8_t reg) {
    Wire.beginTransmission(FT5X06_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)FT5X06_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

static void ft5x06_write_reg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(FT5X06_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

static bool _touch_ok = false;  // set to true only if chip probe succeeds

static bool ft5x06_read_touch(uint16_t *x, uint16_t *y) {
    if (!_touch_ok) return false;
    uint8_t num = ft5x06_read_reg(FT5X06_REG_NUMTOUCH) & 0x0F;
    if (num == 0 || num > 5) return false;

    // Read 4 bytes: XH, XL, YH, YL
    Wire.beginTransmission(FT5X06_ADDR);
    Wire.write(FT5X06_REG_TOUCH1_XH);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)FT5X06_ADDR, (uint8_t)4);
    if (Wire.available() < 4) return false;

    uint8_t xh = Wire.read();
    uint8_t xl = Wire.read();
    uint8_t yh = Wire.read();
    uint8_t yl = Wire.read();

    uint16_t rx = ((xh & 0x0F) << 8) | xl;
    uint16_t ry = ((yh & 0x0F) << 8) | yl;

    // Mirror for 180° display rotation
    *x = (SCREEN_WIDTH  - 1) - rx;
    *y = (SCREEN_HEIGHT - 1) - ry;
    return true;
}

static void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    static uint16_t last_x = 0, last_y = 0;
    static bool was_pressed = false;
    uint16_t x, y;
    if (ft5x06_read_touch(&x, &y)) {
        last_x = x;
        last_y = y;
        data->state = LV_INDEV_STATE_PRESSED;
        if (!was_pressed) {
            Serial.printf("[Touch] DOWN x=%d y=%d\n", x, y);
            was_pressed = true;
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        was_pressed = false;
    }
    data->point.x = last_x;
    data->point.y = last_y;
}

void lvgl_touch_init() {
    // Reset FT5x06 via PCA9535 P07 (pca already initialized by lvgl_display_init)
    pca.direction(PCA95x5::Port::P07, PCA95x5::Direction::OUT);
    pca.write(PCA95x5::Port::P07, PCA95x5::Level::L);
    delay(20);
    pca.write(PCA95x5::Port::P07, PCA95x5::Level::H);
    delay(50);

    // Probe chip: check if device ACKs on I2C
    Wire.beginTransmission(FT5X06_ADDR);
    uint8_t ack = Wire.endTransmission();
    Serial.printf("[Touch] I2C probe 0x%02X: ack=%d (%s)\n", FT5X06_ADDR, ack, ack == 0 ? "OK" : "FAIL");

    if (ack == 0) {
        uint8_t chip_id = ft5x06_read_reg(0xA3);
        uint8_t vendor  = ft5x06_read_reg(0xA8);
        Serial.printf("[Touch] chipID=0x%02X vendorID=0x%02X\n", chip_id, vendor);
        ft5x06_write_reg(FT5X06_REG_MODE,    0x00);
        ft5x06_write_reg(FT5X06_REG_THGROUP, 70);
        _touch_ok = true;
    } else {
        Serial.println("[Touch] FT5x06 not found at 0x38 — touch disabled");
    }

    // Register LVGL input device
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&indev_drv);
    Serial.println("[Touch] LVGL indev registered.");
}

void lvgl_tick_timer_init() {
    const esp_timer_create_args_t timer_args = {
        .callback = &lvgl_tick_isr,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 5000));
}

// =============================================================================
// Color helper
// =============================================================================
static lv_color_t hex2color(uint32_t hex) {
    return lv_color_make((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF);
}

// =============================================================================
// UIManager constructor
// =============================================================================
UIManager::UIManager()
    : _tabview(nullptr), _tabPage1(nullptr), _tabPage2(nullptr), _tabPage3(nullptr),
      _lblTime(nullptr), _lblDateStr(nullptr),
      _lblIndoorTemp(nullptr), _lblIndoorHumidity(nullptr),
      _lblOutdoorTemp(nullptr), _lblOutdoorHumidity(nullptr), _lblSensorStatus(nullptr),
      _lblGreeting(nullptr), _lblMessage(nullptr),
      _lblWeatherIcon(nullptr), _lblWeatherDesc(nullptr),
      _alertBanner(nullptr), _lblAlertText(nullptr),
      _taSSID(nullptr), _taPassword(nullptr), _taHost(nullptr), _taPort(nullptr),
      _spinboxTZ(nullptr), _btnSave(nullptr), _keyboard(nullptr), _activeTA(nullptr),
      _overlayScreen(nullptr), _spinnerOverlay(nullptr), _lblOverlayMsg(nullptr),
      _dotContainer(nullptr), _btnGear(nullptr),
      _currentPage(0), _overlayVisible(false), _settingsCallback(nullptr) {
    for (int i = 0; i < PAGE_COUNT; i++) _dots[i] = nullptr;
}

// =============================================================================
// Dark theme
// =============================================================================
void UIManager::_applyDarkTheme() {
    lv_theme_t* theme = lv_theme_default_init(
        lv_disp_get_default(),
        hex2color(COLOR_ACCENT),
        hex2color(COLOR_ALERT),
        true,
        &lv_font_montserrat_14
    );
    lv_disp_set_theme(lv_disp_get_default(), theme);
    lv_obj_set_style_bg_color(lv_scr_act(), hex2color(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
}

// =============================================================================
// init
// =============================================================================
void UIManager::init(void (*settingsCallback)(const AppSettings&)) {
    _settingsCallback = settingsCallback;
    _applyDarkTheme();

    lv_obj_t* screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, hex2color(COLOR_BG), 0);

    // Tabview: horizontal swipe, hidden tab bar
    _tabview = lv_tabview_create(screen, LV_DIR_LEFT, 0);
    lv_obj_set_size(_tabview, SCREEN_WIDTH, SCREEN_HEIGHT - 20);
    lv_obj_align(_tabview, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_tabview, hex2color(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(_tabview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_tabview, 0, 0);

    _tabPage1 = lv_tabview_add_tab(_tabview, "Clock");
    _tabPage2 = lv_tabview_add_tab(_tabview, "Message");
    _tabPage3 = lv_tabview_add_tab(_tabview, "Settings");

    lv_obj_set_style_bg_color(_tabPage1, hex2color(COLOR_PAGE1), 0);
    lv_obj_set_style_bg_opa(_tabPage1, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(_tabPage2, hex2color(COLOR_PAGE2), 0);
    lv_obj_set_style_bg_opa(_tabPage2, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(_tabPage3, hex2color(COLOR_PAGE3), 0);
    lv_obj_set_style_bg_opa(_tabPage3, LV_OPA_COVER, 0);

    lv_obj_set_style_pad_all(_tabPage1, 12, 0);
    lv_obj_set_style_pad_all(_tabPage2, 12, 0);
    lv_obj_set_style_pad_all(_tabPage3, 12, 0);

    _createPage1(_tabPage1);
    _createPage2(_tabPage2);
    _createPage3(_tabPage3);
    _createNavDots(screen);
    _createGearButton(screen);
    _createOverlay();

    lv_obj_add_event_cb(_tabview, _onTabChanged, LV_EVENT_VALUE_CHANGED, this);
    _updateNavDots(0);
}

// =============================================================================
// Page 1: Clock & Environment
// =============================================================================
void UIManager::_createPage1(lv_obj_t* parent) {
    // Large time display
    _lblTime = lv_label_create(parent);
    lv_label_set_text(_lblTime, "--:--");
    lv_obj_set_style_text_color(_lblTime, hex2color(COLOR_TIME), 0);
    lv_obj_set_style_text_font(_lblTime, &lv_font_montserrat_48, 0);
    lv_obj_align(_lblTime, LV_ALIGN_TOP_MID, 0, 0);

    // Date string below time
    _lblDateStr = lv_label_create(parent);
    lv_label_set_text(_lblDateStr, "---");
    lv_obj_set_style_text_color(_lblDateStr, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(_lblDateStr, &lv_font_montserrat_18, 0);
    lv_obj_align(_lblDateStr, LV_ALIGN_TOP_MID, 0, 58);

    // Divider line
    lv_obj_t* line = lv_obj_create(parent);
    lv_obj_set_size(line, SCREEN_WIDTH - 24, 1);
    lv_obj_align(line, LV_ALIGN_TOP_MID, 0, 86);
    lv_obj_set_style_bg_color(line, hex2color(COLOR_DIVIDER), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(line, 0, 0);
    lv_obj_set_style_radius(line, 0, 0);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // ---- Indoor column (left) ----
    lv_obj_t* lblIndoorTitle = lv_label_create(parent);
    lv_label_set_text(lblIndoorTitle, "INDOOR");
    lv_obj_set_style_text_color(lblIndoorTitle, hex2color(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(lblIndoorTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_letter_space(lblIndoorTitle, 2, 0);
    lv_obj_align(lblIndoorTitle, LV_ALIGN_TOP_LEFT, 0, 96);

    _lblIndoorTemp = lv_label_create(parent);
    lv_label_set_text(_lblIndoorTemp, "--C");
    lv_obj_set_style_text_color(_lblIndoorTemp, hex2color(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(_lblIndoorTemp, &lv_font_montserrat_28, 0);
    lv_obj_align(_lblIndoorTemp, LV_ALIGN_TOP_LEFT, 0, 112);

    _lblIndoorHumidity = lv_label_create(parent);
    lv_label_set_text(_lblIndoorHumidity, "--%");
    lv_obj_set_style_text_color(_lblIndoorHumidity, hex2color(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(_lblIndoorHumidity, &lv_font_montserrat_18, 0);
    lv_obj_align(_lblIndoorHumidity, LV_ALIGN_TOP_LEFT, 0, 148);

    _lblSensorStatus = lv_label_create(parent);
    lv_label_set_text(_lblSensorStatus, "");
    lv_obj_set_style_text_color(_lblSensorStatus, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(_lblSensorStatus, &lv_font_montserrat_12, 0);
    lv_obj_align(_lblSensorStatus, LV_ALIGN_TOP_LEFT, 0, 170);

    // ---- Outdoor column (right) ----
    lv_obj_t* lblOutdoorTitle = lv_label_create(parent);
    lv_label_set_text(lblOutdoorTitle, "OUTDOOR");
    lv_obj_set_style_text_color(lblOutdoorTitle, hex2color(COLOR_OUTDOOR), 0);
    lv_obj_set_style_text_font(lblOutdoorTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_letter_space(lblOutdoorTitle, 2, 0);
    lv_obj_align(lblOutdoorTitle, LV_ALIGN_TOP_MID, 60, 96);

    _lblOutdoorTemp = lv_label_create(parent);
    lv_label_set_text(_lblOutdoorTemp, "--C");
    lv_obj_set_style_text_color(_lblOutdoorTemp, hex2color(COLOR_OUTDOOR), 0);
    lv_obj_set_style_text_font(_lblOutdoorTemp, &lv_font_montserrat_28, 0);
    lv_obj_align(_lblOutdoorTemp, LV_ALIGN_TOP_MID, 60, 112);

    _lblOutdoorHumidity = lv_label_create(parent);
    lv_label_set_text(_lblOutdoorHumidity, "--%");
    lv_obj_set_style_text_color(_lblOutdoorHumidity, hex2color(COLOR_OUTDOOR), 0);
    lv_obj_set_style_text_font(_lblOutdoorHumidity, &lv_font_montserrat_18, 0);
    lv_obj_align(_lblOutdoorHumidity, LV_ALIGN_TOP_MID, 60, 148);
}

// =============================================================================
// Page 2: Message + Weather + Alert
// =============================================================================
void UIManager::_createPage2(lv_obj_t* parent) {
    // Greeting
    _lblGreeting = lv_label_create(parent);
    lv_label_set_text(_lblGreeting, "Good Morning");
    lv_obj_set_style_text_color(_lblGreeting, hex2color(COLOR_ACCENT), 0);
    lv_obj_set_style_text_font(_lblGreeting, &lv_font_montserrat_22, 0);
    lv_obj_align(_lblGreeting, LV_ALIGN_TOP_LEFT, 0, 0);

    // Message (scrollable area)
    lv_obj_t* msgContainer = lv_obj_create(parent);
    lv_obj_set_size(msgContainer, SCREEN_WIDTH - 24, 120);
    lv_obj_align(msgContainer, LV_ALIGN_TOP_LEFT, 0, 30);
    lv_obj_set_style_bg_color(msgContainer, hex2color(0x0A0A20), 0);
    lv_obj_set_style_bg_opa(msgContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(msgContainer, hex2color(COLOR_DIVIDER), 0);
    lv_obj_set_style_border_width(msgContainer, 1, 0);
    lv_obj_set_style_radius(msgContainer, 6, 0);
    lv_obj_set_style_pad_all(msgContainer, 8, 0);

    _lblMessage = lv_label_create(msgContainer);
    lv_label_set_text(_lblMessage, "Waiting for data...");
    lv_obj_set_style_text_color(_lblMessage, hex2color(COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(_lblMessage, &lv_font_montserrat_18, 0);
    lv_obj_set_width(_lblMessage, SCREEN_WIDTH - 44);
    lv_label_set_long_mode(_lblMessage, LV_LABEL_LONG_WRAP);

    // Weather row
    _lblWeatherIcon = lv_label_create(parent);
    lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_EYE_OPEN);
    lv_obj_set_style_text_color(_lblWeatherIcon, hex2color(COLOR_WEATHER), 0);
    lv_obj_set_style_text_font(_lblWeatherIcon, &lv_font_montserrat_22, 0);
    lv_obj_align(_lblWeatherIcon, LV_ALIGN_TOP_LEFT, 0, 158);

    _lblWeatherDesc = lv_label_create(parent);
    lv_label_set_text(_lblWeatherDesc, "---");
    lv_obj_set_style_text_color(_lblWeatherDesc, hex2color(COLOR_WEATHER), 0);
    lv_obj_set_style_text_font(_lblWeatherDesc, &lv_font_montserrat_18, 0);
    lv_obj_set_width(_lblWeatherDesc, SCREEN_WIDTH - 60);
    lv_label_set_long_mode(_lblWeatherDesc, LV_LABEL_LONG_DOT);
    lv_obj_align(_lblWeatherDesc, LV_ALIGN_TOP_LEFT, 30, 160);

    // Alert banner (hidden by default)
    _alertBanner = lv_obj_create(parent);
    lv_obj_set_size(_alertBanner, SCREEN_WIDTH - 24, 36);
    lv_obj_align(_alertBanner, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(_alertBanner, hex2color(COLOR_ALERT), 0);
    lv_obj_set_style_bg_opa(_alertBanner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_alertBanner, 0, 0);
    lv_obj_set_style_radius(_alertBanner, 4, 0);
    lv_obj_set_style_pad_all(_alertBanner, 4, 0);
    lv_obj_clear_flag(_alertBanner, LV_OBJ_FLAG_SCROLLABLE);

    _lblAlertText = lv_label_create(_alertBanner);
    lv_label_set_text(_lblAlertText, "");
    lv_obj_set_style_text_color(_lblAlertText, hex2color(COLOR_TIME), 0);
    lv_obj_set_style_text_font(_lblAlertText, &lv_font_montserrat_14, 0);
    lv_obj_set_width(_lblAlertText, SCREEN_WIDTH - 40);
    lv_label_set_long_mode(_lblAlertText, LV_LABEL_LONG_DOT);
    lv_obj_align(_lblAlertText, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_add_flag(_alertBanner, LV_OBJ_FLAG_HIDDEN);
}

// =============================================================================
// Page 3: Settings
// =============================================================================
void UIManager::_createPage3(lv_obj_t* parent) {
    lv_obj_t* lblTitle = lv_label_create(parent);
    lv_label_set_text(lblTitle, LV_SYMBOL_SETTINGS "  SETTINGS");
    lv_obj_set_style_text_color(lblTitle, hex2color(COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(lblTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_letter_space(lblTitle, 2, 0);
    lv_obj_align(lblTitle, LV_ALIGN_TOP_LEFT, 0, 0);

    // ---- WiFi SSID ----
    lv_obj_t* lblSSID = lv_label_create(parent);
    lv_label_set_text(lblSSID, "WiFi SSID");
    lv_obj_set_style_text_color(lblSSID, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(lblSSID, &lv_font_montserrat_12, 0);
    lv_obj_align(lblSSID, LV_ALIGN_TOP_LEFT, 0, 22);

    _taSSID = lv_textarea_create(parent);
    lv_obj_set_size(_taSSID, 200, 34);
    lv_obj_align(_taSSID, LV_ALIGN_TOP_LEFT, 0, 36);
    lv_textarea_set_one_line(_taSSID, true);
    lv_textarea_set_placeholder_text(_taSSID, "Network name");
    lv_obj_set_style_text_font(_taSSID, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(_taSSID, _onTAFocused, LV_EVENT_FOCUSED, this);

    // ---- WiFi Password ----
    lv_obj_t* lblPass = lv_label_create(parent);
    lv_label_set_text(lblPass, "Password");
    lv_obj_set_style_text_color(lblPass, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(lblPass, &lv_font_montserrat_12, 0);
    lv_obj_align(lblPass, LV_ALIGN_TOP_LEFT, 216, 22);

    _taPassword = lv_textarea_create(parent);
    lv_obj_set_size(_taPassword, 240, 34);
    lv_obj_align(_taPassword, LV_ALIGN_TOP_LEFT, 216, 36);
    lv_textarea_set_one_line(_taPassword, true);
    lv_textarea_set_password_mode(_taPassword, true);
    lv_textarea_set_placeholder_text(_taPassword, "Password");
    lv_obj_set_style_text_font(_taPassword, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(_taPassword, _onTAFocused, LV_EVENT_FOCUSED, this);

    // ---- Server Host ----
    lv_obj_t* lblHost = lv_label_create(parent);
    lv_label_set_text(lblHost, "Server IP");
    lv_obj_set_style_text_color(lblHost, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(lblHost, &lv_font_montserrat_12, 0);
    lv_obj_align(lblHost, LV_ALIGN_TOP_LEFT, 0, 78);

    _taHost = lv_textarea_create(parent);
    lv_obj_set_size(_taHost, 200, 34);
    lv_obj_align(_taHost, LV_ALIGN_TOP_LEFT, 0, 92);
    lv_textarea_set_one_line(_taHost, true);
    lv_textarea_set_placeholder_text(_taHost, "192.168.1.100");
    lv_obj_set_style_text_font(_taHost, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(_taHost, _onTAFocused, LV_EVENT_FOCUSED, this);

    // ---- Server Port ----
    lv_obj_t* lblPort = lv_label_create(parent);
    lv_label_set_text(lblPort, "Port");
    lv_obj_set_style_text_color(lblPort, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(lblPort, &lv_font_montserrat_12, 0);
    lv_obj_align(lblPort, LV_ALIGN_TOP_LEFT, 216, 78);

    _taPort = lv_textarea_create(parent);
    lv_obj_set_size(_taPort, 100, 34);
    lv_obj_align(_taPort, LV_ALIGN_TOP_LEFT, 216, 92);
    lv_textarea_set_one_line(_taPort, true);
    lv_textarea_set_accepted_chars(_taPort, "0123456789");
    lv_textarea_set_placeholder_text(_taPort, "8080");
    lv_obj_set_style_text_font(_taPort, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(_taPort, _onTAFocused, LV_EVENT_FOCUSED, this);

    // ---- Timezone ----
    lv_obj_t* lblTZ = lv_label_create(parent);
    lv_label_set_text(lblTZ, "Timezone (UTC)");
    lv_obj_set_style_text_color(lblTZ, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_set_style_text_font(lblTZ, &lv_font_montserrat_12, 0);
    lv_obj_align(lblTZ, LV_ALIGN_TOP_LEFT, 332, 78);

    _spinboxTZ = lv_spinbox_create(parent);
    lv_obj_set_size(_spinboxTZ, 100, 34);
    lv_obj_align(_spinboxTZ, LV_ALIGN_TOP_LEFT, 332, 92);
    lv_spinbox_set_range(_spinboxTZ, -12, 14);
    lv_spinbox_set_digit_format(_spinboxTZ, 3, 0);
    lv_spinbox_set_value(_spinboxTZ, TIMEZONE_OFFSET_DEFAULT);
    lv_obj_set_style_text_font(_spinboxTZ, &lv_font_montserrat_14, 0);

    // Spinbox increment/decrement buttons
    lv_obj_t* btnInc = lv_btn_create(parent);
    lv_obj_set_size(btnInc, 28, 28);
    lv_obj_align_to(btnInc, _spinboxTZ, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
    lv_obj_set_style_bg_color(btnInc, hex2color(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(btnInc, _onSpinboxIncrement, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* lblInc = lv_label_create(btnInc);
    lv_label_set_text(lblInc, LV_SYMBOL_PLUS);
    lv_obj_center(lblInc);

    lv_obj_t* btnDec = lv_btn_create(parent);
    lv_obj_set_size(btnDec, 28, 28);
    lv_obj_align_to(btnDec, _spinboxTZ, LV_ALIGN_OUT_LEFT_MID, -4, 0);
    lv_obj_set_style_bg_color(btnDec, hex2color(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(btnDec, _onSpinboxDecrement, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* lblDec = lv_label_create(btnDec);
    lv_label_set_text(lblDec, LV_SYMBOL_MINUS);
    lv_obj_center(lblDec);

    // ---- Save button ----
    _btnSave = lv_btn_create(parent);
    lv_obj_set_size(_btnSave, 160, 40);
    lv_obj_align(_btnSave, LV_ALIGN_TOP_RIGHT, 0, 135);
    lv_obj_set_style_bg_color(_btnSave, hex2color(COLOR_ACCENT), 0);
    lv_obj_add_event_cb(_btnSave, _onSaveClicked, LV_EVENT_SHORT_CLICKED, this);
    lv_obj_t* lblSave = lv_label_create(_btnSave);
    lv_label_set_text(lblSave, LV_SYMBOL_SAVE "  Save & Reboot");
    lv_obj_set_style_text_font(lblSave, &lv_font_montserrat_14, 0);
    lv_obj_center(lblSave);

    // ---- Virtual keyboard (hidden initially) ----
    _keyboard = lv_keyboard_create(lv_scr_act());
    lv_obj_set_size(_keyboard, SCREEN_WIDTH, 160);
    lv_obj_align(_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(_keyboard, _onKeyboardReady, LV_EVENT_READY, this);
    lv_obj_add_event_cb(_keyboard, _onKeyboardReady, LV_EVENT_CANCEL, this);
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
}

// =============================================================================
// Navigation dots
// =============================================================================
void UIManager::_createNavDots(lv_obj_t* parent) {
    _dotContainer = lv_obj_create(parent);
    lv_obj_set_size(_dotContainer, PAGE_COUNT * (NAV_DOT_SIZE + NAV_DOT_GAP), NAV_DOT_SIZE + 8);
    lv_obj_align(_dotContainer, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_bg_opa(_dotContainer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_dotContainer, 0, 0);
    lv_obj_set_style_pad_all(_dotContainer, 0, 0);
    lv_obj_clear_flag(_dotContainer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(_dotContainer, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(_dotContainer, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_cross_place(_dotContainer, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_flex_track_place(_dotContainer, LV_FLEX_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_column(_dotContainer, NAV_DOT_GAP - NAV_DOT_SIZE, 0);

    for (int i = 0; i < PAGE_COUNT; i++) {
        _dots[i] = lv_obj_create(_dotContainer);
        lv_obj_set_size(_dots[i], NAV_DOT_SIZE, NAV_DOT_SIZE);
        lv_obj_set_style_radius(_dots[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(_dots[i], 0, 0);
        lv_obj_set_style_bg_color(_dots[i], hex2color(COLOR_DOT_INACTIVE), 0);
        lv_obj_clear_flag(_dots[i], LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    }
}

void UIManager::_updateNavDots(int activePage) {
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (_dots[i]) {
            if (i == activePage) {
                lv_obj_set_style_bg_color(_dots[i], hex2color(COLOR_DOT_ACTIVE), 0);
                lv_obj_set_size(_dots[i], NAV_DOT_SIZE + 4, NAV_DOT_SIZE);
                lv_obj_set_style_radius(_dots[i], 4, 0);
            } else {
                lv_obj_set_style_bg_color(_dots[i], hex2color(COLOR_DOT_INACTIVE), 0);
                lv_obj_set_size(_dots[i], NAV_DOT_SIZE, NAV_DOT_SIZE);
                lv_obj_set_style_radius(_dots[i], LV_RADIUS_CIRCLE, 0);
            }
        }
    }
    _currentPage = activePage;
}

// =============================================================================
// Gear icon button (always visible, top-right)
// =============================================================================
void UIManager::_createGearButton(lv_obj_t* parent) {
    _btnGear = lv_btn_create(parent);
    lv_obj_set_size(_btnGear, 36, 36);
    lv_obj_align(_btnGear, LV_ALIGN_TOP_RIGHT, -4, 4);
    lv_obj_set_style_bg_color(_btnGear, hex2color(0x333355), 0);
    lv_obj_set_style_bg_opa(_btnGear, LV_OPA_70, 0);
    lv_obj_set_style_border_width(_btnGear, 0, 0);
    lv_obj_set_style_radius(_btnGear, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(_btnGear, _onGearClicked, LV_EVENT_SHORT_CLICKED, this);

    lv_obj_t* ico = lv_label_create(_btnGear);
    lv_label_set_text(ico, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(ico, hex2color(COLOR_TEXT_SECONDARY), 0);
    lv_obj_center(ico);
    lv_obj_move_foreground(_btnGear);
}

// =============================================================================
// Overlay (WiFi / error)
// =============================================================================
void UIManager::_createOverlay() {
    _overlayScreen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(_overlayScreen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_align(_overlayScreen, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(_overlayScreen, hex2color(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(_overlayScreen, LV_OPA_90, 0);
    lv_obj_set_style_border_width(_overlayScreen, 0, 0);
    lv_obj_set_style_radius(_overlayScreen, 0, 0);
    lv_obj_clear_flag(_overlayScreen, LV_OBJ_FLAG_SCROLLABLE);

    _spinnerOverlay = lv_spinner_create(_overlayScreen, 1000, 60);
    lv_obj_set_size(_spinnerOverlay, 60, 60);
    lv_obj_align(_spinnerOverlay, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_arc_color(_spinnerOverlay, hex2color(COLOR_ACCENT), LV_PART_INDICATOR);

    _lblOverlayMsg = lv_label_create(_overlayScreen);
    lv_label_set_text(_lblOverlayMsg, "Connecting...");
    lv_obj_set_style_text_color(_lblOverlayMsg, hex2color(COLOR_TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(_lblOverlayMsg, &lv_font_montserrat_18, 0);
    lv_obj_align(_lblOverlayMsg, LV_ALIGN_CENTER, 0, 50);

    lv_obj_add_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
}

// =============================================================================
// Public: showConnecting / showError / hideOverlay
// =============================================================================
void UIManager::showConnecting(const String& ssid) {
    if (!_overlayScreen) return;
    String msg = "Connecting to WiFi...\n" + ssid;
    lv_label_set_text(_lblOverlayMsg, msg.c_str());
    lv_obj_clear_flag(_spinnerOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_overlayScreen);
    _overlayVisible = true;
}

void UIManager::showError(const String& msg) {
    if (!_overlayScreen) return;
    lv_label_set_text(_lblOverlayMsg, msg.c_str());
    lv_obj_add_flag(_spinnerOverlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_overlayScreen);
    _overlayVisible = true;
}

void UIManager::hideOverlay() {
    if (_overlayScreen && _overlayVisible) {
        lv_obj_add_flag(_overlayScreen, LV_OBJ_FLAG_HIDDEN);
        _overlayVisible = false;
    }
}

void UIManager::goToSettings() {
    if (_tabview) {
        lv_tabview_set_act(_tabview, 2, LV_ANIM_ON);
        _updateNavDots(2);
    }
}

// =============================================================================
// populateSettings — pre-fill settings textareas
// =============================================================================
void UIManager::populateSettings(const AppSettings& s) {
    if (_taSSID)    lv_textarea_set_text(_taSSID,    s.wifiSSID.c_str());
    if (_taPassword) lv_textarea_set_text(_taPassword, s.wifiPassword.c_str());
    if (_taHost)    lv_textarea_set_text(_taHost,    s.serverHost.c_str());
    if (_taPort)    lv_textarea_set_text(_taPort,    String(s.serverPort).c_str());
    if (_spinboxTZ) lv_spinbox_set_value(_spinboxTZ, s.timezoneOffset);
}

// =============================================================================
// updateData — update UI with server data
// =============================================================================
void UIManager::updateData(const DisplayData& data) {
    hideOverlay();

    if (!data.valid) return;

    // Page 1: time + date + outdoor
    if (_lblTime)    lv_label_set_text(_lblTime, data.time.isEmpty() ? "--:--" : data.time.c_str());
    if (_lblDateStr) lv_label_set_text(_lblDateStr, data.date.c_str());

    if (_lblOutdoorTemp) {
        String t = data.tempOutdoor.isEmpty() ? "--" : data.tempOutdoor;
        // Ensure there's a unit suffix
        if (t.indexOf('C') < 0 && t.indexOf('F') < 0 && t != "--")
            t += "C";
        lv_label_set_text(_lblOutdoorTemp, t.c_str());
    }
    if (_lblOutdoorHumidity) {
        String h = data.humidityOutdoor.isEmpty() ? "--" : data.humidityOutdoor;
        if (h != "--" && h.indexOf('%') < 0) h += "%";
        lv_label_set_text(_lblOutdoorHumidity, h.c_str());
    }

    // Page 2: greeting + message + weather + alert
    if (_lblGreeting) {
        // Build greeting from time field or just use "message" prefix
        String greeting = "Good Morning";
        if (!data.time.isEmpty()) {
            int hour = data.time.substring(0, 2).toInt();
            if (hour >= 5 && hour < 12) greeting = "Good Morning";
            else if (hour >= 12 && hour < 18) greeting = "Good Afternoon";
            else greeting = "Good Evening";
        }
        lv_label_set_text(_lblGreeting, greeting.c_str());
    }

    if (_lblMessage) lv_label_set_text(_lblMessage, data.message.isEmpty() ? "---" : data.message.c_str());

    if (_lblWeatherDesc) lv_label_set_text(_lblWeatherDesc, data.weather.isEmpty() ? "---" : data.weather.c_str());

    if (_lblWeatherIcon) {
        String w = data.weather;
        w.toLowerCase();
        if (w.indexOf("sun") >= 0 || w.indexOf("clear") >= 0)
            lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_POWER);
        else if (w.indexOf("cloud") >= 0 || w.indexOf("overcast") >= 0)
            lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_UPLOAD);
        else if (w.indexOf("rain") >= 0 || w.indexOf("drizzle") >= 0)
            lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_DOWNLOAD);
        else if (w.indexOf("snow") >= 0)
            lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_IMAGE);
        else
            lv_label_set_text(_lblWeatherIcon, LV_SYMBOL_EYE_OPEN);
    }

    // Alert banner
    if (_alertBanner && _lblAlertText) {
        bool hasAlert = !data.alert.isEmpty() && data.alert != "-" && data.alert != "none";
        if (hasAlert) {
            lv_label_set_text(_lblAlertText, data.alert.c_str());
            lv_obj_clear_flag(_alertBanner, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(_alertBanner, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// =============================================================================
// updateSensor — update indoor sensor values
// =============================================================================
void UIManager::updateSensor(float tempC, float humidityPct, bool available) {
    if (!available) {
        if (_lblIndoorTemp)     lv_label_set_text(_lblIndoorTemp, "--C");
        if (_lblIndoorHumidity) lv_label_set_text(_lblIndoorHumidity, "--%");
        if (_lblSensorStatus)   lv_label_set_text(_lblSensorStatus, "No sensor");
        return;
    }

    if (_lblSensorStatus) lv_label_set_text(_lblSensorStatus, "");

    char buf[16];
    if (_lblIndoorTemp) {
        snprintf(buf, sizeof(buf), "%.1fC", tempC);
        lv_label_set_text(_lblIndoorTemp, buf);
    }
    if (_lblIndoorHumidity) {
        snprintf(buf, sizeof(buf), "%.0f%%", humidityPct);
        lv_label_set_text(_lblIndoorHumidity, buf);
    }
}

// =============================================================================
// tick
// =============================================================================
void UIManager::tick() {
    lv_timer_handler();
}

// =============================================================================
// Keyboard helpers
// =============================================================================
void UIManager::_showKeyboard(lv_obj_t* ta) {
    _activeTA = ta;
    lv_keyboard_set_textarea(_keyboard, ta);
    lv_obj_clear_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_keyboard);
}

void UIManager::_hideKeyboard() {
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    _activeTA = nullptr;
}

// =============================================================================
// Static event callbacks
// =============================================================================
void UIManager::_onTabChanged(lv_event_t* e) {
    UIManager* self = (UIManager*)lv_event_get_user_data(e);
    if (!self || !self->_tabview) return;
    uint16_t page = lv_tabview_get_tab_act(self->_tabview);
    self->_updateNavDots(page);
    // Hide keyboard when leaving settings page
    if (page != 2 && self->_keyboard) {
        self->_hideKeyboard();
    }
}

void UIManager::_onTAFocused(lv_event_t* e) {
    UIManager* self = (UIManager*)lv_event_get_user_data(e);
    lv_obj_t* ta = lv_event_get_target(e);
    if (self && ta) {
        self->_showKeyboard(ta);
    }
}

void UIManager::_onKeyboardReady(lv_event_t* e) {
    UIManager* self = (UIManager*)lv_event_get_user_data(e);
    if (self) self->_hideKeyboard();
}

void UIManager::_onSaveClicked(lv_event_t* e) {
    UIManager* self = (UIManager*)lv_event_get_user_data(e);
    if (!self) return;

    self->_hideKeyboard();

    AppSettings s;
    s.wifiSSID      = lv_textarea_get_text(self->_taSSID);
    s.wifiPassword  = lv_textarea_get_text(self->_taPassword);
    s.serverHost    = lv_textarea_get_text(self->_taHost);
    s.serverPort    = (uint16_t)String(lv_textarea_get_text(self->_taPort)).toInt();
    if (s.serverPort == 0) s.serverPort = DATA_ENDPOINT_PORT_DEFAULT;
    s.timezoneOffset = (int8_t)lv_spinbox_get_value(self->_spinboxTZ);
    s.configured    = true;

    if (self->_settingsCallback) {
        self->_settingsCallback(s);
    }
}

void UIManager::_onGearClicked(lv_event_t* e) {
    UIManager* self = (UIManager*)lv_event_get_user_data(e);
    if (self) self->goToSettings();
}

void UIManager::_onSpinboxIncrement(lv_event_t* e) {
    UIManager* self = (UIManager*)lv_event_get_user_data(e);
    if (self && self->_spinboxTZ) lv_spinbox_increment(self->_spinboxTZ);
}

void UIManager::_onSpinboxDecrement(lv_event_t* e) {
    UIManager* self = (UIManager*)lv_event_get_user_data(e);
    if (self && self->_spinboxTZ) lv_spinbox_decrement(self->_spinboxTZ);
}
