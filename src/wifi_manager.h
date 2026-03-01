#pragma once

#include <Arduino.h>
#include <WiFi.h>

class WiFiManager {
public:
    WiFiManager();

    // Connette al WiFi, ritorna true se successo
    bool connect(const char* ssid, const char* password, uint32_t timeout_ms);

    // Verifica se connesso e tenta riconnessione se necessario
    bool ensureConnected();

    // Ritorna true se il WiFi e' connesso
    bool isConnected() const;

    // IP corrente come stringa
    String getIP() const;

    // RSSI del segnale WiFi
    int getRSSI() const;

private:
    bool _connected;
    const char* _ssid;
    const char* _password;
    uint32_t _timeout_ms;
};
