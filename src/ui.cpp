// =============================================================================
// ui.cpp — Hardware display + touch init for SenseCAP Indicator D1 Pro
//
// Contains ONLY hardware-level code:
//   - ST7701S display init (bit-bang SPI Mode 3, Arduino_GFX RGB panel)
//   - FT5x06 touch driver + LVGL indev registration
//   - LVGL tick timer (esp_timer ISR)
//   - Touch calibration state (accessed by settings_page.cpp)
//
// All UI logic lives in ScreenManager (screen_manager.cpp).
// LVGL 9 migration: update lvgl_display_init() and lvgl_touch_init() only.
// =============================================================================
#include "ui.h"
#include "../include/config.h"

#include <Wire.h>
#include <PCA95x5.h>
#include <Arduino_DataBus.h>
#include <databus/Arduino_ESP32RGBPanel.h>
#include <Arduino_GFX.h>
#include <display/Arduino_RGB_Display.h>
#include <lvgl.h>
#include <esp_timer.h>
#include <esp_heap_caps.h>

// =============================================================================
// Global touch indev — used by ScreenManager for gesture detection
// =============================================================================
lv_indev_t* g_touch_indev = nullptr;

// Current touch pressed state — updated by touch_read_cb, read by ScreenManager
bool g_touch_pressed = false;

// =============================================================================
// Hardware objects
// =============================================================================
#define LCD_SCK_PIN  41
#define LCD_MOSI_PIN 48

static PCA9535 pca;
static Arduino_ESP32RGBPanel* rgbpanel = nullptr;
static Arduino_RGB_Display*   gfx      = nullptr;

// Full-frame single buffer in PSRAM — matches Seeed reference implementation.
// A single fullscreen buffer avoids partial-flush tearing on the RGB panel.
static const uint32_t LV_BUF_SIZE = SCREEN_WIDTH * SCREEN_HEIGHT;
static lv_color_t*        lv_buf1 = nullptr;
static lv_disp_draw_buf_t lv_draw_buf;
static lv_disp_drv_t      lv_disp_drv;

// =============================================================================
// Touch calibration state (accessed by settings_page.cpp via extern declarations)
// =============================================================================
int16_t _cal_x0 = 0, _cal_x1 = SCREEN_WIDTH  - 1;
int16_t _cal_y0 = 0, _cal_y1 = SCREEN_HEIGHT - 1;
bool    _cal_valid = false;

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
// =============================================================================
static inline void spi_write_bit(bool bit) {
    digitalWrite(LCD_MOSI_PIN, bit ? HIGH : LOW);
    digitalWrite(LCD_SCK_PIN, LOW);   // falling edge — ST7701S latches here
    digitalWrite(LCD_SCK_PIN, HIGH);  // return to idle
}

static void lcd_spi_write_cmd(uint8_t cmd) {
    spi_write_bit(0);
    for (int i = 7; i >= 0; i--) spi_write_bit((cmd >> i) & 1);
}

static void lcd_spi_write_data(uint8_t data) {
    spi_write_bit(1);
    for (int i = 7; i >= 0; i--) spi_write_bit((data >> i) & 1);
}

static void lcd_cmd(uint8_t cmd) {
    pca.write(PCA95x5::Port::P04, PCA95x5::Level::L);
    lcd_spi_write_cmd(cmd);
    pca.write(PCA95x5::Port::P04, PCA95x5::Level::H);
}

static void lcd_cmd_data(uint8_t cmd, const uint8_t* data, size_t len) {
    pca.write(PCA95x5::Port::P04, PCA95x5::Level::L);
    lcd_spi_write_cmd(cmd);
    for (size_t i = 0; i < len; i++) lcd_spi_write_data(data[i]);
    pca.write(PCA95x5::Port::P04, PCA95x5::Level::H);
}

#define LCD_CMD(c)        lcd_cmd(c)
#define LCD_CMD1(c,d0)    do { uint8_t _d[]={d0}; lcd_cmd_data(c,_d,1); } while(0)
#define LCD_CMD2(c,d0,d1) do { uint8_t _d[]={d0,d1}; lcd_cmd_data(c,_d,2); } while(0)

// =============================================================================
// ST7701S init sequence for SenseCAP Indicator D1 Pro
// =============================================================================
static void lcd_init_sequence() {
    { uint8_t d[] = {0x77, 0x01, 0x00, 0x00, 0x10}; lcd_cmd_data(0xFF, d, 5); }
    LCD_CMD2(0xC0, 0x3B, 0x00);
    LCD_CMD2(0xC1, 0x0D, 0x02);
    LCD_CMD2(0xC2, 0x31, 0x05);
    LCD_CMD1(0xCD, 0x08);
    { uint8_t d[] = {0x00,0x11,0x18,0x0E,0x11,0x06,0x07,0x08,
                     0x07,0x22,0x04,0x12,0x0F,0xAA,0x31,0x18};
      lcd_cmd_data(0xB0, d, 16); }
    { uint8_t d[] = {0x00,0x11,0x19,0x0E,0x12,0x07,0x08,0x08,
                     0x08,0x22,0x04,0x11,0x11,0xA9,0x32,0x18};
      lcd_cmd_data(0xB1, d, 16); }

    { uint8_t d[] = {0x77, 0x01, 0x00, 0x00, 0x11}; lcd_cmd_data(0xFF, d, 5); }
    LCD_CMD1(0xB0, 0x60);
    LCD_CMD1(0xB1, 0x32);
    LCD_CMD1(0xB2, 0x07);
    LCD_CMD1(0xB3, 0x80);
    LCD_CMD1(0xB5, 0x49);
    LCD_CMD1(0xB7, 0x85);
    LCD_CMD1(0xB8, 0x21);
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

    { uint8_t d[] = {0x77, 0x01, 0x00, 0x00, 0x13}; lcd_cmd_data(0xFF, d, 5); }
    LCD_CMD1(0xE5, 0xE4);

    { uint8_t d[] = {0x77, 0x01, 0x00, 0x00, 0x00}; lcd_cmd_data(0xFF, d, 5); }
    LCD_CMD1(0xE0, 0x1F);   // Sunlight readable enhancement
    LCD_CMD(0x21);           // Display Inversion ON (IPS panel)
    LCD_CMD1(0x3A, 0x60);   // Pixel format RGB666
    LCD_CMD(0x11);           // Sleep Out
    delay(120);
    LCD_CMD(0x29);           // Display ON
    delay(20);
}

// =============================================================================
// lvgl_display_init
// =============================================================================
void lvgl_display_init() {
    Wire.begin(39, 40, 400000UL);

    pca.attach(Wire, 0x20);
    pca.polarity(PCA95x5::Polarity::ORIGINAL_ALL);
    pca.direction(PCA95x5::Port::P04, PCA95x5::Direction::OUT);
    pca.direction(PCA95x5::Port::P05, PCA95x5::Direction::OUT);

    pinMode(LCD_SCK_PIN,  OUTPUT);
    pinMode(LCD_MOSI_PIN, OUTPUT);
    digitalWrite(LCD_SCK_PIN,  HIGH);
    digitalWrite(LCD_MOSI_PIN, LOW);

    pca.write(PCA95x5::Port::P04, PCA95x5::Level::H);
    pca.write(PCA95x5::Port::P05, PCA95x5::Level::H);

    pca.write(PCA95x5::Port::P05, PCA95x5::Level::L);
    delay(10);
    pca.write(PCA95x5::Port::P05, PCA95x5::Level::H);
    delay(120);

    lcd_init_sequence();

    // Timing confirmed working on this hardware (reverted to known-good values).
    // prefer_speed left as default (GFX_NOT_DEFINED) — let Arduino_GFX choose PCLK.
    // hsync: pol=1, fp=10, pw=8, bp=50 | vsync: pol=1, fp=10, pw=8, bp=20
    // pclk_active_neg=1 (falling edge) — required by this panel variant.
    rgbpanel = new Arduino_ESP32RGBPanel(
        18, 17, 16, 21,
        4,  3,  2,  1,  0,
        10, 9,  8,  7,  6,  5,
        15, 14, 13, 12, 11,
        1, 10, 8, 50,   // hsync: polarity=1, fp=10, pw=8, bp=50
        1, 10, 8, 20);  // vsync: polarity=1, fp=10, pw=8, bp=20

    gfx = new Arduino_RGB_Display(
        SCREEN_WIDTH, SCREEN_HEIGHT, rgbpanel, DISPLAY_ROTATION, true);
    gfx->begin();
    gfx->fillScreen(BLACK);

    pinMode(GFX_BL, OUTPUT);
    ledcSetup(0, 5000, 8);
    ledcAttachPin(GFX_BL, 0);
    ledcWrite(0, BACKLIGHT_BRIGHTNESS);

    lv_init();
    // Single fullscreen buffer in PSRAM (~460 KB). Matches Seeed reference.
    lv_buf1 = (lv_color_t*)heap_caps_malloc(LV_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_disp_draw_buf_init(&lv_draw_buf, lv_buf1, nullptr, LV_BUF_SIZE);
    lv_disp_drv_init(&lv_disp_drv);
    lv_disp_drv.hor_res     = SCREEN_WIDTH;
    lv_disp_drv.ver_res     = SCREEN_HEIGHT;
    lv_disp_drv.flush_cb    = disp_flush_cb;
    lv_disp_drv.draw_buf    = &lv_draw_buf;
    // full_refresh=1: LVGL redraws the entire frame into the PSRAM buffer each tick,
    // then flushes once. With a single PSRAM buffer and an RGB streaming panel this
    // avoids partial-flush tearing and makes scroll/animation smooth.
    lv_disp_drv.full_refresh = 1;
    lv_disp_drv_register(&lv_disp_drv);
}

// =============================================================================
// FT5x06 touch driver
// =============================================================================
#define FT5X06_ADDR          0x48
#define FT5X06_REG_MODE      0x00
#define FT5X06_REG_NUMTOUCH  0x02
#define FT5X06_REG_TOUCH1_XH 0x03
#define FT5X06_REG_TOUCH1_XL 0x04
#define FT5X06_REG_TOUCH1_YH 0x05
#define FT5X06_REG_TOUCH1_YL 0x06
#define FT5X06_REG_THGROUP   0x80

static bool _touch_ok = false;

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

// Raw read (mirror only, no calibration) — used by settings_page.cpp during calibration
bool ft5x06_read_touch(uint16_t* x, uint16_t* y) {
    if (!_touch_ok) return false;
    uint8_t num = ft5x06_read_reg(FT5X06_REG_NUMTOUCH) & 0x0F;
    if (num == 0 || num > 5) return false;
    Wire.beginTransmission(FT5X06_ADDR);
    Wire.write(FT5X06_REG_TOUCH1_XH);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)FT5X06_ADDR, (uint8_t)4);
    if (Wire.available() < 4) return false;
    uint8_t xh = Wire.read(), xl = Wire.read();
    uint8_t yh = Wire.read(), yl = Wire.read();
    uint16_t rx = ((xh & 0x0F) << 8) | xl;
    uint16_t ry = ((yh & 0x0F) << 8) | yl;

    // Mirror for 180° rotation, then apply calibration
    int32_t mx = (SCREEN_WIDTH  - 1) - (int32_t)rx;
    int32_t my = (SCREEN_HEIGHT - 1) - (int32_t)ry;
    if (_cal_valid) {
        int32_t rx_range = _cal_x1 - _cal_x0;
        int32_t ry_range = _cal_y1 - _cal_y0;
        if (rx_range != 0) mx = (mx - _cal_x0) * (SCREEN_WIDTH  - 1) / rx_range;
        if (ry_range != 0) my = (my - _cal_y0) * (SCREEN_HEIGHT - 1) / ry_range;
    }
    if (mx < 0) mx = 0; if (mx >= SCREEN_WIDTH)  mx = SCREEN_WIDTH  - 1;
    if (my < 0) my = 0; if (my >= SCREEN_HEIGHT) my = SCREEN_HEIGHT - 1;
    *x = (uint16_t)mx;
    *y = (uint16_t)my;
    return true;
}

static void touch_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    static uint16_t last_x = 0, last_y = 0;
    uint16_t x, y;
    if (ft5x06_read_touch(&x, &y)) {
        last_x = x; last_y = y;
        data->state   = LV_INDEV_STATE_PRESSED;
        g_touch_pressed = true;   // update global for ScreenManager gesture detection
    } else {
        data->state   = LV_INDEV_STATE_RELEASED;
        g_touch_pressed = false;
    }
    data->point.x = last_x;
    data->point.y = last_y;
}

// =============================================================================
// lvgl_touch_init
// =============================================================================
void lvgl_touch_init() {
    pca.direction(PCA95x5::Port::P07, PCA95x5::Direction::OUT);
    pca.write(PCA95x5::Port::P07, PCA95x5::Level::L);
    delay(20);
    pca.write(PCA95x5::Port::P07, PCA95x5::Level::H);
    delay(50);

    Wire.beginTransmission(FT5X06_ADDR);
    uint8_t ack = Wire.endTransmission();
    Serial.printf("[Touch] I2C probe 0x%02X: %s\n", FT5X06_ADDR, ack == 0 ? "OK" : "FAIL");

    if (ack == 0) {
        uint8_t chip_id = ft5x06_read_reg(0xA3);
        uint8_t vendor  = ft5x06_read_reg(0xA8);
        Serial.printf("[Touch] chipID=0x%02X vendorID=0x%02X\n", chip_id, vendor);
        ft5x06_write_reg(FT5X06_REG_MODE,    0x00);
        ft5x06_write_reg(FT5X06_REG_THGROUP, 70);
        _touch_ok = true;
    } else {
        Serial.println("[Touch] FT5x06 not found — touch disabled");
    }

    // Register LVGL input device and save pointer for ScreenManager
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read_cb;
    g_touch_indev = lv_indev_drv_register(&indev_drv);
    Serial.println("[Touch] LVGL indev registered.");
}

// =============================================================================
// lvgl_tick_timer_init
// =============================================================================
void lvgl_tick_timer_init() {
    const esp_timer_create_args_t args = {
        .callback = &lvgl_tick_isr,
        .name     = "lvgl_tick"
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, 5000));
}
