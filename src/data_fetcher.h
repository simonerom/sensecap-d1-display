// =============================================================================
// data_fetcher.h — HTTP client for /layout.xml and /data.json
// =============================================================================
#pragma once

#include <Arduino.h>
#include <map>
#include <vector>

// Parsed data.json payload
struct DataPayload {
    std::map<String, String>              scalars;  // flat key→value strings
    std::map<String, std::vector<String>> arrays;   // "news", "events" (flattened to strings)
    bool     valid     = false;
    uint32_t fetchedAt = 0;
};

class DataFetcher {
public:
    DataFetcher();

    // Configure server at runtime (from NVS settings)
    void configure(const String& host, uint16_t port, uint32_t timeout_ms);

    // Fetch /layout.xml — returns PSRAM-allocated null-terminated buffer.
    // Caller MUST call ps_free() on the returned pointer when done.
    // cachedVersion: in — version string previously received (e.g. "1.0.0"); empty = first fetch.
    // outVersion: out — receives the X-Layout-Version header from response.
    // Returns nullptr if version is unchanged (cachedVersion == server version) or on error.
    // Check lastError().isEmpty() to distinguish "unchanged" from "error".
    char* fetchLayout(const String& cachedVersion, String& outVersion, size_t& outLen);

    // Fetch /data.json and parse into DataPayload.
    bool fetchData(DataPayload& out);

    const String& lastError()    const { return _lastError; }
    int           lastHttpCode() const { return _lastHttpCode; }

private:
    String   _host;
    uint16_t _port    = 8765;
    uint32_t _timeout = 5000;
    String   _lastError;
    int      _lastHttpCode = 0;

    // Core HTTP GET. Populates _lastHttpCode and _lastError.
    // Returns the response body (headers stripped). Empty on error.
    // outEtag: if non-null, filled with ETag response header value.
    String _httpGet(const char* path,
                    const char* extraHeaders = nullptr,
                    String* outEtag = nullptr);

    bool _parseDataJson(const String& body, DataPayload& out);
};
