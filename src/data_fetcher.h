#pragma once

#include <Arduino.h>

// Data structure returned from the JSON endpoint
struct DisplayData {
    // From server JSON
    String time;             // "HH:MM"
    String date;             // "Monday, March 2"
    String message;          // daily message / greeting
    String weather;          // weather description
    String tempOutdoor;      // "12C"
    String humidityOutdoor;  // "65"
    String alert;            // alert text, empty if none

    // From Grove sensor (local, updated separately)
    float  tempIndoor;
    float  humidityIndoor;
    bool   sensorAvailable;

    bool     valid;       // true if server data received successfully
    uint32_t fetchedAt;   // millis() at time of fetch
};

class DataFetcher {
public:
    DataFetcher();

    // Configure host/port/path at runtime (from NVS settings)
    void configure(const String& host, uint16_t port, const String& path, uint32_t timeout_ms);

    // Perform fetch and populate 'data'. Returns true on success.
    bool fetch(DisplayData& data);

    int lastHttpCode() const { return _lastHttpCode; }
    const String& lastError() const { return _lastError; }

private:
    String   _host;
    uint16_t _port;
    String   _path;
    uint32_t _timeout_ms;
    int      _lastHttpCode;
    String   _lastError;
};
