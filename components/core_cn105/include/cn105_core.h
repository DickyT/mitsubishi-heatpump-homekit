#pragma once

#include <cstddef>
#include <cstdint>

namespace cn105_core {

inline constexpr size_t kPacketLen = 22;
inline constexpr size_t kConnectLen = 8;
inline constexpr size_t kMaxHexLen = (kPacketLen * 3);

struct Packet {
    uint8_t bytes[kPacketLen] = {};
    size_t length = 0;
};

struct SetCommand {
    bool hasPower = false;
    const char* power = nullptr;

    bool hasMode = false;
    const char* mode = nullptr;

    bool hasTemperatureF = false;
    int temperatureF = 77;

    bool hasFan = false;
    const char* fan = nullptr;

    bool hasVane = false;
    const char* vane = nullptr;

    bool hasWideVane = false;
    const char* wideVane = nullptr;
};

struct MockState {
    bool connected = true;
    const char* power = "OFF";
    const char* mode = "COOL";
    int targetTemperatureF = 77;
    int roomTemperatureF = 75;
    const char* fan = "AUTO";
    const char* vane = "AUTO";
    const char* wideVane = "|";
    bool operating = false;
    int compressorFrequencyHz = 0;
    int inputPowerW = 0;
    float energyKwh = 0.0f;
    const char* lastPacketHex = "";
    const char* lastError = "";
};

struct DecodedPacket {
    bool checksumOk = false;
    uint8_t command = 0;
    uint8_t dataLength = 0;
    uint8_t infoCode = 0;
    char type[24] = "unknown";
    char summary[192] = "";
};

uint8_t checksum(const uint8_t* bytes, size_t len);
bool bytesToHex(const uint8_t* bytes, size_t len, char* out, size_t out_len);
bool parseHex(const char* hex, uint8_t* out, size_t max_len, size_t* out_len, char* error, size_t error_len);

bool buildConnectPacket(Packet* packet, char* error, size_t error_len);
bool buildInfoPacket(uint8_t infoCode, Packet* packet, char* error, size_t error_len);
bool buildSetPacket(const SetCommand& command, Packet* packet, char* error, size_t error_len);
bool decodePacket(const uint8_t* bytes, size_t len, DecodedPacket* decoded, char* error, size_t error_len);

void initMockState();
MockState getMockState();
bool applySetPacketToMock(const uint8_t* bytes, size_t len, char* error, size_t error_len);
bool runSelfTest(char* error, size_t error_len);

}  // namespace cn105_core
