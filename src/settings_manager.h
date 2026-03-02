#pragma once

#include <Arduino.h>

struct AppSettings {
    String  wifiSSID;
    String  wifiPassword;
    String  serverHost;
    uint16_t serverPort;
    int8_t  timezoneOffset;  // hours from UTC, -12..+14
    bool    configured;      // true if settings were saved by user
};

// Touch calibration: maps raw ADC [x0..x1] -> [0..SCREEN_WIDTH-1]
//                              raw ADC [y0..y1] -> [0..SCREEN_HEIGHT-1]
struct TouchCalibration {
    int16_t x0, x1;  // raw X at left / right calibration target
    int16_t y0, y1;  // raw Y at top  / bottom calibration target
    bool    valid;
};

class SettingsManager {
public:
    SettingsManager();

    // Load settings from NVS. Returns false if no settings saved yet.
    bool load(AppSettings& settings);

    // Save settings to NVS and set configured=true.
    void save(const AppSettings& settings);

    // Load/save touch calibration.
    bool loadCalibration(TouchCalibration& cal);
    void saveCalibration(const TouchCalibration& cal);

    // Clear all settings (factory reset).
    void clear();
};
