#pragma once

#include <cstdint>
#include <cmath>
#include <cstring>

// The main CN105 packets we care about in this project are 22 bytes long:
//   5-byte base header + 16-byte payload + 1-byte checksum
// For example:
//   SET  = FC 41 01 30 10 [16 bytes payload] [checksum]
//   INFO = FC 42 01 30 10 [16 bytes payload] [checksum]
static const int PACKET_LEN = 22;

// The CONNECT handshake is shorter and only 8 bytes long:
//   FC 5A 01 30 02 CA 01 A8
// This simply tells the indoor unit that a controller is present and wants to start a session.
static const int CONNECT_LEN = 8;

// The first 8 bytes of a SET packet are the fixed header:
//   [0] 0xFC  start byte
//   [1] 0x41  SET command
//   [2] 0x01  fixed
//   [3] 0x30  fixed
//   [4] 0x10  payload length = 16
//   [5] 0x01  fixed
//   [6]       control bits #1 (which fields should be applied)
//   [7]       control bits #2 (extended control bits)
static const int HEADER_LEN = 8;

// The fixed INFO header is only the first 5 bytes:
//   [0] 0xFC  start byte
//   [1] 0x42  INFO request
//   [2] 0x01  fixed
//   [3] 0x30  fixed
//   [4] 0x10  payload length = 16
// The actual request code is usually the first payload byte, data[0],
// for example 0x02 / 0x03 / 0x06 / 0x09.
static const int INFOHEADER_LEN = 5;

// CONNECT handshake packet.
// We use 0x5A as the standard handshake and usually expect a 0x7A reply.
// In the current offline-builder phase this packet is not sent yet, but we keep it
// here because the real UART stage will use the same constant.
static const uint8_t CONNECT[CONNECT_LEN] = {
    0xFC, 0x5A, 0x01, 0x30, 0x02, 0xCA, 0x01, 0xA8
};

// Fixed SET command header.
// The variable control content starts after this header:
//   byte[6] / byte[7]  control bits
//   byte[8]            power
//   byte[9]            mode
//   byte[10]           legacy integer temperature encoding
//   byte[11]           fan speed
//   byte[12]           vertical vane
//   byte[18]           horizontal vane
//   byte[19]           high-precision temperature encoding
//   byte[21]           checksum
static const uint8_t HEADER[HEADER_LEN] = {
    0xFC, 0x41, 0x01, 0x30, 0x10, 0x01, 0x00, 0x00
};

// Fixed INFO request header.
// The actual "what should be read" selector lives in the payload:
//   0x02 = current settings
//   0x03 = room / outside temperature
//   0x06 = operating status
//   0x09 = stage / sub-mode
static const uint8_t INFOHEADER[INFOHEADER_LEN] = {
    0xFC, 0x42, 0x01, 0x30, 0x10
};

// Bit masks for SET byte[6].
// These bits do not carry the values themselves. They declare which fields in the packet
// should be applied by the indoor unit.
// For example:
//   temperature only      -> byte[6] |= 0x04
//   mode + fan speed      -> byte[6] |= 0x02 | 0x08
// If a bit is not set, the indoor unit will usually ignore that field even if a value is present.
static const uint8_t CONTROL_PACKET_1[5] = {
    0x01, 0x02, 0x04, 0x08, 0x10
};

// Extended control bits for SET byte[7].
// We currently use only one bit here:
//   bit0 = horizontal vane (wide vane)
// So to update wideVane, we need both:
//   byte[7] |= 0x01
//   byte[18] = actual wideVane value
static const uint8_t CONTROL_PACKET_2[1] = {
    0x01
};

static const uint8_t POWER[2] = {0x00, 0x01};
static const char* POWER_MAP[2] = {"OFF", "ON"};

static const uint8_t MODE[5] = {0x01, 0x02, 0x03, 0x07, 0x08};
static const char* MODE_MAP[5] = {"HEAT", "DRY", "COOL", "FAN", "AUTO"};

static const uint8_t TEMP[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
};
// This table only matches the legacy temperature field at byte[10].
// There are no half-degree values here because byte[10] only supports integer Celsius values.
// For example:
//   0x00 -> 31C
//   0x07 -> 24C
//   0x0F -> 16C
//
// If we want to express 24.5C / 25.5C style half-degree setpoints, TEMP_MAP is not enough.
// Those values must be encoded through the high-precision field at byte[19].
//
// byte[19] uses this formula:
//   encoded = tempC * 2 + 128
//
// For example:
//   25.0C -> 178
//   25.5C -> 179
//   26.0C -> 180
static const int TEMP_MAP[16] = {
    31, 30, 29, 28, 27, 26, 25, 24,
    23, 22, 21, 20, 19, 18, 17, 16
};

static const uint8_t FAN[6] = {0x00, 0x01, 0x02, 0x03, 0x05, 0x06};
static const char* FAN_MAP[6] = {"AUTO", "QUIET", "1", "2", "3", "4"};

static const uint8_t VANE[7] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x07};
static const char* VANE_MAP[7] = {"AUTO", "1", "2", "3", "4", "5", "SWING"};

static const uint8_t WIDEVANE[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x08, 0x0C, 0x00};
static const char* WIDEVANE_MAP[8] = {"<<", "<", "|", ">", ">>", "<>", "SWING", "AIRFLOW CONTROL"};

// Legacy room-temperature response bytes are a simple 10C-41C linear range.
// When data[6] is zero in a 0x03 response, data[3] falls back to this table.
static const uint8_t ROOM_TEMP[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F
};
static const int ROOM_TEMP_MAP[32] = {
    10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25,
    26, 27, 28, 29, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41
};

static const uint8_t STAGE[7] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
static const char* STAGE_MAP[7] = {"IDLE", "LOW", "GENTLE", "MEDIUM", "MODERATE", "HIGH", "DIFFUSE"};

static const uint8_t SUB_MODE[5] = {0x00, 0x01, 0x02, 0x04, 0x08};
static const char* SUB_MODE_MAP[5] = {"NORMAL", "WARMUP", "DEFROST", "PREHEAT", "STANDBY"};

template <typename T>
static T lookupByteMapValue(const T* map, const uint8_t* bytes, int len, uint8_t value) {
    for (int i = 0; i < len; i++) {
        if (bytes[i] == value) {
            return map[i];
        }
    }
    return map[0];
}

static int lookupByteMapIndex(const char** map, int len, const char* value) {
    for (int i = 0; i < len; i++) {
        if (strcmp(map[i], value) == 0) {
            return i;
        }
    }
    return -1;
}

struct cn105SetPacketBuild {
    uint8_t bytes[PACKET_LEN];
    bool valid;
    uint8_t control1;
    uint8_t control2;
    float encodedTemperatureC;
    bool usedHighPrecisionTemperature;

    void reset() {
        memset(bytes, 0, sizeof(bytes));
        valid = false;
        control1 = 0;
        control2 = 0;
        encodedTemperatureC = NAN;
        usedHighPrecisionTemperature = false;
    }
};

struct cn105InfoResponseBuild {
    uint8_t bytes[PACKET_LEN];
    bool valid;
    uint8_t infoCode;

    void reset() {
        memset(bytes, 0, sizeof(bytes));
        valid = false;
        infoCode = 0;
    }
};

struct heatpumpSettings {
    const char* power;
    const char* mode;
    float temperature;
    const char* fan;
    const char* vane;
    const char* wideVane;
    bool iSee;
    bool connected;

    void reset() {
        power = nullptr;
        mode = nullptr;
        temperature = -1.0f;
        fan = nullptr;
        vane = nullptr;
        wideVane = nullptr;
        iSee = false;
        connected = false;
    }
};

struct heatpumpStatus {
    float roomTemperature;
    float outsideAirTemperature;
    bool operating;
    float compressorFrequency;
    float inputPower;
    float kWh;
    float runtimeHours;
    const char* stage;
    const char* subMode;

    void reset() {
        roomTemperature = NAN;
        outsideAirTemperature = NAN;
        operating = false;
        compressorFrequency = 0.0f;
        inputPower = 0.0f;
        kWh = 0.0f;
        runtimeHours = 0.0f;
        stage = nullptr;
        subMode = nullptr;
    }
};
