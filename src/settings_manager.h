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

class SettingsManager {
public:
    SettingsManager();

    // Load settings from NVS. Returns false if no settings saved yet.
    bool load(AppSettings& settings);

    // Save settings to NVS and set configured=true.
    void save(const AppSettings& settings);

    // Clear all settings (factory reset).
    void clear();
};
