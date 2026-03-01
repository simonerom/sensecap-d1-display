#pragma once

#include <Arduino.h>

// Grove I2C temperature/humidity sensor driver
// Supports SHT40 (0x44) with fallback to DHT20 (0x38)
class GroveSensor {
public:
    enum Type { NONE, SHT40, DHT20 };

    GroveSensor();

    // Initialize I2C and detect sensor type. Returns detected type.
    Type begin(uint8_t sda, uint8_t scl);

    // Read sensor. Returns true on success.
    // temperature in Celsius, humidity in %RH
    bool read(float& temperature, float& humidity);

    Type sensorType() const { return _type; }
    bool isAvailable() const { return _type != NONE; }

private:
    Type _type;

    bool _readSHT40(float& t, float& h);
    bool _readDHT20(float& t, float& h);
};
