#include "data_fetcher.h"
#include "../include/config.h"

#include <WiFi.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

DataFetcher::DataFetcher(const char* host, uint16_t port, const char* path, uint32_t timeout_ms)
    : _host(host), _port(port), _path(path), _timeout_ms(timeout_ms),
      _lastHttpCode(0) {}

bool DataFetcher::fetch(DisplayData& data) {
    data.valid = false;
    _lastError = "";
    _lastHttpCode = 0;

    WiFiClient client;
    client.setTimeout(_timeout_ms / 1000);

    DEBUG_PRINTF("[HTTP] Connecting to %s:%d%s\n", _host, _port, _path);

    if (!client.connect(_host, _port)) {
        _lastError = "Connection failed to " + String(_host);
        DEBUG_PRINTLN("[HTTP] " + _lastError);
        return false;
    }

    // Send HTTP/1.1 request
    client.print(String("GET ") + _path + " HTTP/1.1\r\n" +
                 "Host: " + _host + ":" + _port + "\r\n" +
                 "Connection: close\r\n" +
                 "Accept: application/json\r\n" +
                 "\r\n");

    // Wait for response with timeout
    uint32_t start = millis();
    while (client.available() == 0) {
        if (millis() - start > _timeout_ms) {
            _lastError = "Response wait timeout";
            DEBUG_PRINTLN("[HTTP] " + _lastError);
            client.stop();
            return false;
        }
        delay(10);
    }

    // Read HTTP status line
    String statusLine = client.readStringUntil('\n');
    statusLine.trim();
    DEBUG_PRINTLN("[HTTP] Status: " + statusLine);

    // Extract HTTP status code (e.g. "HTTP/1.1 200 OK")
    int spaceIdx = statusLine.indexOf(' ');
    if (spaceIdx > 0) {
        _lastHttpCode = statusLine.substring(spaceIdx + 1, spaceIdx + 4).toInt();
    }

    if (_lastHttpCode != 200) {
        _lastError = "HTTP " + String(_lastHttpCode);
        client.stop();
        return false;
    }

    // Skip HTTP headers (look for empty line)
    while (client.available()) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) break;
    }

    // Read JSON body
    String body = "";
    uint32_t readStart = millis();
    while (client.available() || client.connected()) {
        if (millis() - readStart > _timeout_ms) {
            _lastError = "Body read timeout";
            client.stop();
            return false;
        }
        if (client.available()) {
            body += client.readString();
        }
        delay(1);
    }
    client.stop();

    body.trim();
    DEBUG_PRINTLN("[HTTP] Body: " + body);

    if (body.isEmpty()) {
        _lastError = "Empty body";
        return false;
    }

    // Parse JSON
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        _lastError = String("JSON parse error: ") + err.c_str();
        DEBUG_PRINTLN("[HTTP] " + _lastError);
        return false;
    }

    data.date    = doc["date"]    | "";
    data.message = doc["message"] | "";
    data.weather = doc["weather"] | "";
    data.alert   = doc["alert"]   | "";
    data.valid   = true;
    data.fetchedAt = millis();

    DEBUG_PRINTLN("[HTTP] Data received OK");
    DEBUG_PRINTF("[HTTP]   date:    %s\n", data.date.c_str());
    DEBUG_PRINTF("[HTTP]   message: %s\n", data.message.c_str());
    DEBUG_PRINTF("[HTTP]   weather: %s\n", data.weather.c_str());
    DEBUG_PRINTF("[HTTP]   alert:   %s\n", data.alert.c_str());

    return true;
}
