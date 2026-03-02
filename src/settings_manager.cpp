#include "settings_manager.h"
#include "../include/config.h"

#include <Preferences.h>

SettingsManager::SettingsManager() {}

bool SettingsManager::load(AppSettings& settings) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);  // read-only

    settings.configured = prefs.getBool(NVS_KEY_VALID, false);

    settings.wifiSSID     = prefs.getString(NVS_KEY_SSID, WIFI_SSID_DEFAULT);
    settings.wifiPassword = prefs.getString(NVS_KEY_PASS, WIFI_PASSWORD_DEFAULT);
    settings.serverHost   = prefs.getString(NVS_KEY_HOST, DATA_ENDPOINT_HOST_DEFAULT);
    settings.serverPort   = (uint16_t)prefs.getUInt(NVS_KEY_PORT, DATA_ENDPOINT_PORT_DEFAULT);
    settings.timezoneOffset = (int8_t)prefs.getInt(NVS_KEY_TZ, TIMEZONE_OFFSET_DEFAULT);

    prefs.end();

    DEBUG_PRINTF("[Settings] Loaded: configured=%d ssid=%s host=%s port=%d tz=%d\n",
                 settings.configured,
                 settings.wifiSSID.c_str(),
                 settings.serverHost.c_str(),
                 settings.serverPort,
                 settings.timezoneOffset);

    return settings.configured;
}

void SettingsManager::save(const AppSettings& settings) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);  // read-write

    prefs.putString(NVS_KEY_SSID, settings.wifiSSID);
    prefs.putString(NVS_KEY_PASS, settings.wifiPassword);
    prefs.putString(NVS_KEY_HOST, settings.serverHost);
    prefs.putUInt(NVS_KEY_PORT,   settings.serverPort);
    prefs.putInt(NVS_KEY_TZ,      settings.timezoneOffset);
    prefs.putBool(NVS_KEY_VALID,  true);

    prefs.end();

    DEBUG_PRINTLN("[Settings] Saved to NVS.");
}

bool SettingsManager::loadCalibration(TouchCalibration& cal) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    cal.valid = prefs.getBool(NVS_KEY_CAL_VALID, false);
    cal.x0 = (int16_t)prefs.getInt(NVS_KEY_CAL_X0, 0);
    cal.x1 = (int16_t)prefs.getInt(NVS_KEY_CAL_X1, 479);
    cal.y0 = (int16_t)prefs.getInt(NVS_KEY_CAL_Y0, 0);
    cal.y1 = (int16_t)prefs.getInt(NVS_KEY_CAL_Y1, 479);
    prefs.end();
    DEBUG_PRINTF("[Settings] Cal loaded: valid=%d x=[%d..%d] y=[%d..%d]\n",
                 cal.valid, cal.x0, cal.x1, cal.y0, cal.y1);
    return cal.valid;
}

void SettingsManager::saveCalibration(const TouchCalibration& cal) {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool(NVS_KEY_CAL_VALID, true);
    prefs.putInt(NVS_KEY_CAL_X0, cal.x0);
    prefs.putInt(NVS_KEY_CAL_X1, cal.x1);
    prefs.putInt(NVS_KEY_CAL_Y0, cal.y0);
    prefs.putInt(NVS_KEY_CAL_Y1, cal.y1);
    prefs.end();
    DEBUG_PRINTF("[Settings] Cal saved: x=[%d..%d] y=[%d..%d]\n",
                 cal.x0, cal.x1, cal.y0, cal.y1);
}

void SettingsManager::clear() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    DEBUG_PRINTLN("[Settings] NVS cleared.");
}
