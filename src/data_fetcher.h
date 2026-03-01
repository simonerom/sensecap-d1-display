#pragma once

#include <Arduino.h>

// Struttura dati restituita dall'endpoint
struct DisplayData {
    String date;
    String message;
    String weather;
    String alert;
    bool   valid;       // true se i dati sono stati ricevuti con successo
    uint32_t fetchedAt; // millis() al momento del fetch
};

class DataFetcher {
public:
    DataFetcher(const char* host, uint16_t port, const char* path, uint32_t timeout_ms);

    // Esegue il fetch e popola 'data'. Ritorna true se successo.
    bool fetch(DisplayData& data);

    // Ritorna l'ultimo codice HTTP ricevuto
    int lastHttpCode() const { return _lastHttpCode; }

    // Ritorna l'ultimo messaggio di errore
    const String& lastError() const { return _lastError; }

private:
    const char* _host;
    uint16_t    _port;
    const char* _path;
    uint32_t    _timeout_ms;
    int         _lastHttpCode;
    String      _lastError;
};
