// =============================================================================
// rp2040_comm.cpp
// =============================================================================
#include "rp2040_comm.h"
#include "../include/config.h"
#include <driver/uart.h>
#include <string.h>

#define ACK_PAYLOAD "ACK"

void RP2040Comm::begin() {
    uart_config_t cfg = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_driver_install((uart_port_t)_UART_NUM, _BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config((uart_port_t)_UART_NUM, &cfg);
    uart_set_pin((uart_port_t)_UART_NUM, _TX_PIN, _RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    DEBUG_PRINTLN("[RP2040] UART2 initialized, sending power-on");
    _sendCmd(RP_PKT_CMD_POWER_ON);
}

void RP2040Comm::poll() {
    int len = uart_read_bytes((uart_port_t)_UART_NUM, _rxBuf,
                              sizeof(_rxBuf) - 1, 10 / portTICK_PERIOD_MS);
    if (len <= 0) return;

    // Split on 0x00 delimiters (COBS frame boundaries)
    uint8_t* start = _rxBuf;
    uint8_t* end   = _rxBuf;
    uint8_t* limit = _rxBuf + len;

    while (start < limit) {
        end = start;
        while (end < limit && *end != 0x00) end++;
        if (end > start) {
            _processChunk(start, end - start);
        }
        start = end + 1;
    }
}

void RP2040Comm::_processChunk(const uint8_t* chunk, size_t len) {
    size_t outLen = _cobsDecode(_decoded, sizeof(_decoded), chunk, len);
    if (outLen >= 2) {  // at least type + 1 byte payload
        _handlePacket(_decoded, outLen);
    }
}

void RP2040Comm::_handlePacket(const uint8_t* data, size_t len) {
    uint8_t pktType = data[0];
    float   value   = 0;

    if (len >= 5) {
        memcpy(&value, &data[1], sizeof(float));
    }

    switch (pktType) {
        case RP_PKT_AHT20_TEMP:
            _data.aht20_temp  = value;
            _data.aht20_valid = true;
            DEBUG_PRINTF("[RP2040] AHT20 T=%.1f\n", value);
            sendAck();
            break;
        case RP_PKT_AHT20_HUM:
            _data.aht20_hum   = value;
            _data.aht20_valid = true;
            DEBUG_PRINTF("[RP2040] AHT20 H=%.1f\n", value);
            sendAck();
            break;
        case RP_PKT_SCD41_TEMP:
            _data.scd41_temp  = value;
            _data.scd41_valid = true;
            sendAck();
            break;
        case RP_PKT_SCD41_HUM:
            _data.scd41_hum   = value;
            _data.scd41_valid = true;
            sendAck();
            break;
        case RP_PKT_SCD41_CO2:
            _data.scd41_co2   = value;
            _data.scd41_valid = true;
            DEBUG_PRINTF("[RP2040] CO2=%.0f ppm\n", value);
            sendAck();
            break;
        case RP_PKT_TVOC:
            _data.tvoc       = value;
            _data.tvoc_valid = true;
            DEBUG_PRINTF("[RP2040] tVOC=%.0f\n", value);
            sendAck();
            break;
        default:
            break;
    }
}

void RP2040Comm::sendAck() {
    _sendCmd(RP_PKT_ACK, ACK_PAYLOAD, 3);
}

void RP2040Comm::_sendCmd(uint8_t cmd, const void* payload, uint8_t payloadLen) {
    uint8_t src[32] = {};
    uint8_t enc[36] = {};

    src[0] = cmd;
    uint8_t srcLen = 1;
    if (payload && payloadLen > 0 && payloadLen <= 31) {
        memcpy(&src[1], payload, payloadLen);
        srcLen += payloadLen;
    }

    size_t encLen = _cobsEncode(enc, sizeof(enc), src, srcLen);
    if (encLen > 0) {
        enc[encLen] = 0x00;  // COBS frame terminator
        uart_write_bytes((uart_port_t)_UART_NUM, enc, encLen + 1);
    }
}

// =============================================================================
// COBS decode (ported from Seeed reference implementation)
// =============================================================================
size_t RP2040Comm::_cobsDecode(uint8_t* dst, size_t dstLen,
                                const uint8_t* src, size_t srcLen) {
    if (!dst || !src || srcLen == 0) return 0;

    const uint8_t* srcEnd = src + srcLen;
    uint8_t*       dstPtr = dst;
    uint8_t*       dstEnd = dst + dstLen;

    while (src < srcEnd) {
        uint8_t code = *src++;
        if (code == 0) break;  // zero byte in input
        code--;

        size_t remaining = srcEnd - src;
        if (code > remaining) code = (uint8_t)remaining;
        if (code > (dstEnd - dstPtr)) return 0;  // overflow

        for (uint8_t i = code; i != 0; i--) {
            *dstPtr++ = *src++;
        }
        if (src >= srcEnd) break;

        if (code != 0xFE) {
            if (dstPtr >= dstEnd) return 0;
            *dstPtr++ = 0;
        }
    }
    return (size_t)(dstPtr - dst);
}

// =============================================================================
// COBS encode
// =============================================================================
size_t RP2040Comm::_cobsEncode(uint8_t* dst, size_t dstLen,
                                const uint8_t* src, size_t srcLen) {
    if (!dst || !src || srcLen == 0) return 0;

    const uint8_t* srcEnd        = src + srcLen;
    uint8_t*       dstStart      = dst;
    uint8_t*       dstEnd        = dst + dstLen;
    uint8_t*       codePtr       = dst++;
    uint8_t        searchLen     = 1;

    while (src < srcEnd) {
        if (dst >= dstEnd) return 0;
        uint8_t b = *src++;
        if (b == 0) {
            *codePtr  = searchLen;
            codePtr   = dst++;
            searchLen = 1;
        } else {
            *dst++ = b;
            searchLen++;
            if (searchLen == 0xFF) {
                *codePtr  = searchLen;
                codePtr   = dst++;
                searchLen = 1;
            }
        }
    }
    *codePtr = searchLen;
    return (size_t)(dst - dstStart);
}
