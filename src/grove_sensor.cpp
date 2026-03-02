#include "grove_sensor.h"
#include "../include/config.h"
#include <Wire.h>

#define GROVE_WIRE Wire

GroveSensor::GroveSensor() : _type(NONE), _sht40Addr(SHT40_ADDR) {}

GroveSensor::Type GroveSensor::begin(uint8_t sda, uint8_t scl) {
    DEBUG_PRINTF("[Grove] Probing on shared I2C bus (SDA=%d SCL=%d)\n", sda, scl);

    // Try SHT40 at 0x44, 0x45 (0x48 is FT5x06 touch — excluded)
    uint8_t shtAddrs[] = { SHT40_ADDR, SHT40_ADDR_ALT1 };
    for (uint8_t addr : shtAddrs) {
        GROVE_WIRE.beginTransmission(addr);
        if (GROVE_WIRE.endTransmission() == 0) {
            _type = SHT40;
            _sht40Addr = addr;
            DEBUG_PRINTF("[Grove] SHT40 detected at 0x%02X\n", addr);
            return _type;
        }
    }

    // Try DHT20/AHT20 at 0x38
    GROVE_WIRE.beginTransmission(DHT20_ADDR);
    if (GROVE_WIRE.endTransmission() == 0) {
        _type = DHT20;
        DEBUG_PRINTLN("[Grove] DHT20/AHT20 detected at 0x38");
        GROVE_WIRE.beginTransmission(DHT20_ADDR);
        GROVE_WIRE.write(0x71);
        GROVE_WIRE.endTransmission();
        delay(10);
        return _type;
    }

    DEBUG_PRINTLN("[Grove] No external T/H sensor detected");
    _type = NONE;
    return _type;
}

bool GroveSensor::read(float& temperature, float& humidity) {
    if (_type == SHT40) return _readSHT40(temperature, humidity);
    if (_type == DHT20) return _readDHT20(temperature, humidity);
    return false;
}

// ---- SHT40 ----
bool GroveSensor::_readSHT40(float& t, float& h) {
    GROVE_WIRE.beginTransmission(_sht40Addr);
    GROVE_WIRE.write(0xFD);
    if (GROVE_WIRE.endTransmission() != 0) return false;
    delay(10);
    if (GROVE_WIRE.requestFrom((uint8_t)_sht40Addr, (uint8_t)6) != 6) return false;
    uint8_t buf[6];
    for (int i = 0; i < 6; i++) buf[i] = GROVE_WIRE.read();
    uint16_t rawT = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t rawH = ((uint16_t)buf[3] << 8) | buf[4];
    t = -45.0f + 175.0f * ((float)rawT / 65535.0f);
    h = -6.0f  + 125.0f * ((float)rawH / 65535.0f);
    if (h < 0.0f) h = 0.0f;
    if (h > 100.0f) h = 100.0f;
    return true;
}

// ---- DHT20/AHT20 ----
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
    if (buf[0] & 0x80) return false;
    uint32_t rawH = ((uint32_t)buf[1] << 12) | ((uint32_t)buf[2] << 4) | (buf[3] >> 4);
    uint32_t rawT = (((uint32_t)buf[3] & 0x0F) << 16) | ((uint32_t)buf[4] << 8) | buf[5];
    h = (float)rawH / 1048576.0f * 100.0f;
    t = (float)rawT / 1048576.0f * 200.0f - 50.0f;
    return true;
}
