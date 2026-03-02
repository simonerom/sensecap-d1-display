#pragma once
// =============================================================================
// rp2040_comm.h — ESP32 ↔ RP2040 UART communication
//
// The RP2040 reads all on-board sensors and sends values to the ESP32 via
// UART2 (TX=19, RX=20, 115200 bps) using COBS framing.
//
// Packet format (after COBS decode):
//   byte 0: packet type
//   bytes 1-4: float value (little-endian)
//
// Sensor packet types:
//   0xB0 SCD41 temperature (°C)
//   0xB1 SCD41 humidity (%RH)
//   0xB2 SCD41 CO2 (ppm)
//   0xB3 AHT20 temperature (°C)  ← Grove indoor sensor
//   0xB4 AHT20 humidity (%RH)
//   0xB5 tVOC index (float)
// =============================================================================
#include <Arduino.h>

// Packet type constants
#define RP_PKT_ACK              0x00
#define RP_PKT_CMD_COLLECT      0xA0
#define RP_PKT_CMD_BEEP_ON      0xA1
#define RP_PKT_CMD_BEEP_OFF     0xA2
#define RP_PKT_CMD_SHUTDOWN     0xA3
#define RP_PKT_CMD_POWER_ON     0xA4
#define RP_PKT_SCD41_TEMP       0xB0
#define RP_PKT_SCD41_HUM        0xB1
#define RP_PKT_SCD41_CO2        0xB2
#define RP_PKT_AHT20_TEMP       0xB3
#define RP_PKT_AHT20_HUM        0xB4
#define RP_PKT_TVOC             0xB5

struct RP2040Data {
    float aht20_temp   = 0;
    float aht20_hum    = 0;
    float scd41_temp   = 0;
    float scd41_hum    = 0;
    float scd41_co2    = 0;
    float tvoc         = 0;
    bool  aht20_valid  = false;
    bool  scd41_valid  = false;
    bool  tvoc_valid   = false;
};

class RP2040Comm {
public:
    // Initialize UART2 and send power-on command to RP2040.
    void begin();

    // Process any pending UART bytes, update internal data.
    // Call frequently (e.g. every 500ms from taskSensor).
    void poll();

    // Get a snapshot of the latest sensor values.
    RP2040Data getData() const { return _data; }

    // Send ACK back to RP2040 (called after each sensor packet)
    void sendAck();

private:
    RP2040Data _data;

    static const int _UART_NUM = 2;
    static const int _TX_PIN   = 19;
    static const int _RX_PIN   = 20;
    static const int _BUF_SIZE = 512;

    uint8_t _rxBuf[512];
    uint8_t _decoded[512];

    void _processChunk(const uint8_t* chunk, size_t len);
    void _handlePacket(const uint8_t* data, size_t len);
    void _sendCmd(uint8_t cmd, const void* payload = nullptr, uint8_t payloadLen = 0);

    // COBS decode
    size_t _cobsDecode(uint8_t* dst, size_t dstLen, const uint8_t* src, size_t srcLen);
    size_t _cobsEncode(uint8_t* dst, size_t dstLen, const uint8_t* src, size_t srcLen);
};
