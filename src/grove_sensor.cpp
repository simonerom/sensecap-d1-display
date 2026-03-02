#include "grove_sensor.h"
#include "../include/config.h"

// Grove sensor uses Wire1 (I2C bus 1, SDA=2/SCL=3) to avoid conflict with
// the display I2C bus (Wire on SDA=39/SCL=40 for PCA9535 and FT5x06).
#define GROVE_WIRE Wire1
#include <Wire.h>

GroveSensor::GroveSensor() : _type(NONE) {}

GroveSensor::Type GroveSensor::begin(uint8_t sda, uint8_t scl) {
    GROVE_WIRE.begin(sda, scl);
    GROVE_WIRE.setClock(100000);

    // Try SHT40 first (0x44)
    GROVE_WIRE.beginTransmission(SHT40_ADDR);
    if (GROVE_WIRE.endTransmission() == 0) {
        _type = SHT40;
        DEBUG_PRINTLN("[Sensor] SHT40 detected at 0x44");
        return _type;
    }

    // Try DHT20 (0x38)
    GROVE_WIRE.beginTransmission(DHT20_ADDR);
    if (GROVE_WIRE.endTransmission() == 0) {
        _type = DHT20;
        DEBUG_PRINTLN("[Sensor] DHT20 detected at 0x38");
        // DHT20 initialization: send 0x71 status check
        GROVE_WIRE.beginTransmission(DHT20_ADDR);
        GROVE_WIRE.write(0x71);
        GROVE_WIRE.endTransmission();
        delay(10);
        return _type;
    }

    DEBUG_PRINTLN("[Sensor] No Grove sensor detected");
    _type = NONE;
    return _type;
}

bool GroveSensor::read(float& temperature, float& humidity) {
    if (_type == SHT40) return _readSHT40(temperature, humidity);
    if (_type == DHT20) return _readDHT20(temperature, humidity);
    return false;
}

// ---- SHT40 ----
// Measure high precision: command 0xFD, read 6 bytes after 10ms
bool GroveSensor::_readSHT40(float& t, float& h) {
    GROVE_WIRE.beginTransmission(SHT40_ADDR);
    GROVE_WIRE.write(0xFD);  // measure high precision
    if (GROVE_WIRE.endTransmission() != 0) return false;

    delay(10);

    if (GROVE_WIRE.requestFrom((uint8_t)SHT40_ADDR, (uint8_t)6) != 6) return false;

    uint8_t buf[6];
    for (int i = 0; i < 6; i++) buf[i] = GROVE_WIRE.read();

    // bytes 0-1: temp raw, byte 2: CRC
    // bytes 3-4: humi raw, byte 5: CRC
    uint16_t rawT = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t rawH = ((uint16_t)buf[3] << 8) | buf[4];

    t = -45.0f + 175.0f * ((float)rawT / 65535.0f);
    h = -6.0f  + 125.0f * ((float)rawH / 65535.0f);
    if (h < 0.0f) h = 0.0f;
    if (h > 100.0f) h = 100.0f;

    return true;
}

// ---- DHT20 ----
// Trigger measurement: {0xAC, 0x33, 0x00}, wait 80ms, read 7 bytes
bool GroveSensor::_readDHT20(float& t, float& h) {
    GROVE_WIRE.beginTransmission(DHT20_ADDR);
    GROVE_WIRE.write(0xAC);
    GROVE_WIRE.write(0x33);
    GROVE_WIRE.write(0x00);
    if (GROVE_WIRE.endTransmission() != 0) return false;

    delay(80);

    if (GROVE_WIRE.requestFrom((uint8_t)DHT20_ADDR, (uint8_t)7) != 7) return false;

    uint8_t buf[7];
    for (int i = 0; i < 7; i++) buf[i] = GROVE_WIRE.read();

    // Check busy bit (bit 7 of byte 0)
    if (buf[0] & 0x80) return false;

    uint32_t rawH = ((uint32_t)buf[1] << 12) | ((uint32_t)buf[2] << 4) | (buf[3] >> 4);
    uint32_t rawT = (((uint32_t)buf[3] & 0x0F) << 16) | ((uint32_t)buf[4] << 8) | buf[5];

    h = (float)rawH / 1048576.0f * 100.0f;
    t = (float)rawT / 1048576.0f * 200.0f - 50.0f;

    return true;
}
