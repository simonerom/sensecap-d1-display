#include "wifi_manager.h"
#include "../include/config.h"

WiFiManager::WiFiManager()
    : _connected(false), _ssid(nullptr), _password(nullptr), _timeout_ms(10000) {}

bool WiFiManager::connect(const char* ssid, const char* password, uint32_t timeout_ms) {
    _ssid = ssid;
    _password = password;
    _timeout_ms = timeout_ms;

    DEBUG_PRINTF("[WiFi] Connecting to: %s\n", ssid);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);  // We handle reconnect manually
    WiFi.begin(ssid, password);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > timeout_ms) {
            DEBUG_PRINTLN("[WiFi] Connection timeout!");
            WiFi.disconnect(true);
            _connected = false;
            return false;
        }
        wl_status_t st = WiFi.status();
        if (st == WL_NO_SSID_AVAIL || st == WL_CONNECT_FAILED) {
            DEBUG_PRINTF("[WiFi] Connection failed (status=%d)\n", st);
            WiFi.disconnect(true);
            _connected = false;
            return false;
        }
        delay(500);
        DEBUG_PRINT(".");
    }

    _connected = true;
    DEBUG_PRINTLN();
    DEBUG_PRINTF("[WiFi] Connected! IP: %s  RSSI: %d dBm\n",
                 WiFi.localIP().toString().c_str(),
                 WiFi.RSSI());
    return true;
}

bool WiFiManager::ensureConnected() {
    if (WiFi.status() == WL_CONNECTED) {
        _connected = true;
        return true;
    }

    DEBUG_PRINTLN("[WiFi] Connection lost, reconnecting...");
    _connected = false;

    WiFi.disconnect();
    delay(1000);
    return connect(_ssid, _password, _timeout_ms);
}

bool WiFiManager::isConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

String WiFiManager::getIP() const {
    return WiFi.localIP().toString();
}

int WiFiManager::getRSSI() const {
    return WiFi.RSSI();
}
