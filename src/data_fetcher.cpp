// =============================================================================
// data_fetcher.cpp
// =============================================================================
#include "data_fetcher.h"
#include "../include/config.h"

#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

DataFetcher::DataFetcher()
    : _port(DATA_ENDPOINT_PORT_DEFAULT), _timeout(HTTP_TIMEOUT_MS) {
    _host = DATA_ENDPOINT_HOST_DEFAULT;
}

void DataFetcher::configure(const String& host, uint16_t port, uint32_t timeout_ms) {
    _host    = host;
    _port    = port;
    _timeout = timeout_ms;
}

// =============================================================================
// _httpGet — core HTTP/1.1 GET over raw WiFiClient
// =============================================================================
String DataFetcher::_httpGet(const char* path,
                              const char* extraHeaders,
                              String* outEtag) {
    _lastError    = "";
    _lastHttpCode = 0;

    WiFiClient client;
    client.setTimeout(_timeout / 1000);

    DEBUG_PRINTF("[HTTP] GET %s:%d%s\n", _host.c_str(), _port, path);

    if (!client.connect(_host.c_str(), _port)) {
        _lastError = "Connection failed to " + _host;
        return String();
    }

    // Request
    String req = String("GET ") + path + " HTTP/1.1\r\n"
               + "Host: " + _host + ":" + _port + "\r\n"
               + "Connection: close\r\n";
    if (extraHeaders) req += extraHeaders;
    req += "\r\n";
    client.print(req);

    // Wait for response
    uint32_t t0 = millis();
    while (client.available() == 0) {
        if (millis() - t0 > _timeout) {
            _lastError = "Response timeout";
            client.stop();
            return String();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Status line
    String statusLine = client.readStringUntil('\n');
    statusLine.trim();
    int sp = statusLine.indexOf(' ');
    if (sp > 0) _lastHttpCode = statusLine.substring(sp + 1, sp + 4).toInt();
    DEBUG_PRINTF("[HTTP] Status: %d\n", _lastHttpCode);

    // Headers — look for X-Layout-Version
    String layoutVer;
    while (client.available()) {
        String line = client.readStringUntil('\n');
        line.trim();
        if (line.isEmpty()) break;
        if (outEtag && line.startsWith("X-Layout-Version:")) {
            layoutVer = line.substring(17);
            layoutVer.trim();
        }
    }
    if (outEtag) *outEtag = layoutVer;

    if (_lastHttpCode == 304) {
        client.stop();
        return String();  // not modified
    }

    if (_lastHttpCode != 200) {
        _lastError = "HTTP " + String(_lastHttpCode);
        client.stop();
        return String();
    }

    // Body — read in chunks with watchdog feed to avoid WDT timeout on large responses
    String body;
    body.reserve(8192);
    t0 = millis();
    uint8_t chunk[256];
    while (client.connected() || client.available()) {
        if (millis() - t0 > _timeout) {
            _lastError = "Body read timeout";
            client.stop();
            return String();
        }
        int n = client.read(chunk, sizeof(chunk));
        if (n > 0) {
            body.concat((const char*)chunk, n);
            t0 = millis();  // reset timeout on progress
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));  // yield — resets WDT, allows IDLE task to run
        }
    }
    client.stop();
    body.trim();
    return body;
}

// =============================================================================
// fetchLayout — uses X-Layout-Version header for change detection
// =============================================================================
char* DataFetcher::fetchLayout(const String& cachedVersion, String& outVersion, size_t& outLen) {
    outLen = 0;
    outVersion = "";

    String serverVersion;
    String body = _httpGet(LAYOUT_ENDPOINT_PATH, nullptr, &serverVersion);
    outVersion = serverVersion;

    if (body.isEmpty()) {
        // Could be HTTP error or genuinely empty
        return nullptr;
    }

    // If version matches cached, no rebuild needed
    if (cachedVersion.length() > 0 && serverVersion == cachedVersion) {
        DEBUG_PRINTF("[HTTP] Layout version unchanged (%s), skip rebuild.\n", serverVersion.c_str());
        _lastError = "";  // not an error
        return nullptr;
    }

    outLen = body.length();
    char* buf = (char*)heap_caps_malloc(outLen + 1, MALLOC_CAP_SPIRAM);
    if (!buf) {
        _lastError = "heap_caps_malloc failed for layout XML";
        return nullptr;
    }
    memcpy(buf, body.c_str(), outLen);
    buf[outLen] = '\0';

    DEBUG_PRINTF("[HTTP] Layout XML received: %u bytes, version=%s\n",
                 outLen, serverVersion.c_str());
    return buf;
}

// =============================================================================
// fetchData
// =============================================================================
bool DataFetcher::fetchData(DataPayload& out) {
    out.valid = false;
    String body = _httpGet(DATA_ENDPOINT_PATH);
    if (body.isEmpty()) return false;

    return _parseDataJson(body, out);
}

// =============================================================================
// _parseDataJson — ArduinoJson 7 deserialization into DataPayload
// =============================================================================
bool DataFetcher::_parseDataJson(const String& body, DataPayload& out) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (err) {
        _lastError = String("JSON: ") + err.c_str();
        return false;
    }

    // Extract all scalar string keys
    for (JsonPair kv : doc.as<JsonObject>()) {
        if (kv.value().is<JsonArray>()) continue;
        out.scalars[kv.key().c_str()] = kv.value().as<String>();
    }

    // news array → flat strings
    if (doc["news"].is<JsonArray>()) {
        std::vector<String> items;
        for (JsonVariant v : doc["news"].as<JsonArray>()) {
            items.push_back(v.as<String>());
        }
        out.arrays["news"] = items;
    }

    // events array → "HH:MM  date  Title" strings
    if (doc["events"].is<JsonArray>()) {
        std::vector<String> items;
        for (JsonObject ev : doc["events"].as<JsonArray>()) {
            String line;
            const char* t = ev["time"] | "";
            const char* d = ev["date"] | "";
            const char* title = ev["title"] | "";
            if (t[0]) { line += t; line += "  "; }
            if (d[0]) { line += d; line += "  "; }
            line += title;
            items.push_back(line);
        }
        out.arrays["events"] = items;
    }

    out.valid     = true;
    out.fetchedAt = millis();

    DEBUG_PRINTLN("[HTTP] Data JSON parsed OK");
    DEBUG_PRINTF("[HTTP]   scalars: %u  arrays: %u\n",
                 out.scalars.size(), out.arrays.size());
    return true;
}
