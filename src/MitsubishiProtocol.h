#pragma once

#include <Arduino.h>
#include <math.h>

#include "AppConfig.h"
#include "FahrenheitSupport.h"
#include "MitsubishiTypes.h"

class MitsubishiProtocol {
public:
    explicit MitsubishiProtocol(HardwareSerial* serial = nullptr)
        : serial_(serial) {
        currentSettings_.reset();
        currentStatus_.reset();
        lastBuild_.reset();
        lastResponseBuild_.reset();
        resetRxState();
        applyMockDefaults();
    }

    void connect() {
        connected_ = true;
        currentSettings_.connected = true;
    }

    void processInput() {
        if (!serial_) {
            return;
        }

        uint32_t now = millis();
        if (rxIndex_ > 0 && now - lastRxByteMs_ > AppConfig::CN105_RX_TIMEOUT_MS) {
            Serial.printf("[CN105 RX] Timeout after %lums, dropping partial packet (%d bytes)\n",
                          static_cast<unsigned long>(now - lastRxByteMs_),
                          rxIndex_);
            resetRxState();
        }

        while (serial_->available() > 0) {
            int raw = serial_->read();
            if (raw < 0) {
                break;
            }
            processIncomingByte(static_cast<uint8_t>(raw));
        }
    }

    void loopPollCycle() {
        currentStatus_.runtimeHours = millis() / 3600000.0f;
    }

    bool isConnected() const {
        return connected_;
    }

    const heatpumpSettings& getCurrentSettings() const {
        return currentSettings_;
    }

    const heatpumpStatus& getCurrentStatus() const {
        return currentStatus_;
    }

    void setDemoConnected(bool connected) {
        connected_ = connected;
        currentSettings_.connected = connected;
    }

    void setDemoRoomTemperature(float value) {
        currentStatus_.roomTemperature = value;
    }

    void applyMockRemoteSettings(const String& power,
                                 const String& mode,
                                 float temperatureF,
                                 const String& fan,
                                 const String& vane,
                                 const String& wideVane) {
        powerStorage_ = power;
        modeStorage_ = mode;
        fanStorage_ = fan;
        vaneStorage_ = vane;
        wideVaneStorage_ = wideVane;

        currentSettings_.temperature = fahrenheitSupport_.setpointFahrenheitToDeviceCelsius(temperatureF);
        currentSettings_.connected = true;
        connected_ = true;

        syncSettingPointers();
        updateDerivedMockStatus();
        buildCurrentSetPacket();
    }

    bool decodeMockResponse(uint8_t infoCode) {
        cn105InfoResponseBuild build;
        if (!buildMockInfoResponsePacket(infoCode, &build)) {
            return false;
        }

        if (!parseInfoResponsePacket(build.bytes, PACKET_LEN)) {
            return false;
        }

        lastResponseBuild_ = build;
        return true;
    }

    bool decodeAllMockResponses() {
        bool ok = true;
        ok = decodeMockResponse(0x02) && ok;
        ok = decodeMockResponse(0x03) && ok;
        ok = decodeMockResponse(0x06) && ok;
        return ok;
    }

    bool decodeRawResponseHex(const String& rawHex, String* errorMessage = nullptr) {
        uint8_t bytes[PACKET_LEN];
        String parseError;
        if (!parseHexPacketString(rawHex, bytes, PACKET_LEN, &parseError)) {
            if (errorMessage) {
                *errorMessage = parseError;
            }
            return false;
        }

        if (bytes[0] != 0xFC) {
            if (errorMessage) {
                *errorMessage = "invalid packet start byte, expected FC";
            }
            return false;
        }

        if (bytes[1] != 0x62) {
            if (errorMessage) {
                *errorMessage = "only CN105 0x62 response packets are supported here";
            }
            return false;
        }

        if (bytes[4] != 0x10) {
            if (errorMessage) {
                *errorMessage = "unexpected payload length byte, expected 10";
            }
            return false;
        }

        uint8_t expectedChecksum = calcCheckSum(bytes, PACKET_LEN - 1);
        if (expectedChecksum != bytes[PACKET_LEN - 1]) {
            if (errorMessage) {
                *errorMessage = "checksum mismatch: expected " + byteToHex(expectedChecksum) +
                                " but received " + byteToHex(bytes[PACKET_LEN - 1]);
            }
            return false;
        }

        if (!parseInfoResponsePacket(bytes, PACKET_LEN)) {
            if (errorMessage) {
                *errorMessage = "packet rejected by CN105 response parser";
            }
            return false;
        }

        memcpy(lastResponseBuild_.bytes, bytes, PACKET_LEN);
        lastResponseBuild_.valid = true;
        lastResponseBuild_.infoCode = bytes[5];
        if (errorMessage) {
            *errorMessage = "";
        }
        return true;
    }

    float getTargetTemperatureF() const {
        return fahrenheitSupport_.deviceCelsiusToSetpointFahrenheit(currentSettings_.temperature);
    }

    float getRoomTemperatureF() const {
        return fahrenheitSupport_.deviceCelsiusToDisplayFahrenheit(currentStatus_.roomTemperature);
    }

    float getOutsideTemperatureF() const {
        return fahrenheitSupport_.deviceCelsiusToDisplayFahrenheit(currentStatus_.outsideAirTemperature);
    }

    const cn105SetPacketBuild& getLastBuild() const {
        return lastBuild_;
    }

    const cn105InfoResponseBuild& getLastResponseBuild() const {
        return lastResponseBuild_;
    }

    String buildMockConfigPreview() const {
        String config = "# CN105 draft generated by WebUI\n";
        config += "power=" + powerStorage_ + "\n";
        config += "mode=" + modeStorage_ + "\n";
        config += "temperature_f=" + String(getTargetTemperatureF(), 0) + "\n";
        config += "temperature_c_raw=" + String(currentSettings_.temperature, 1) + "\n";
        config += "fan=" + fanStorage_ + "\n";
        config += "vane=" + vaneStorage_ + "\n";
        config += "wideVane=" + wideVaneStorage_ + "\n";
        config += "transport=mock\n";
        config += "status=preview_only\n";
        return config;
    }

    String getLastPacketHex() const {
        return packetBytesToHex(lastBuild_.bytes, lastBuild_.valid, PACKET_LEN);
    }

    String getLastResponsePacketHex() const {
        return packetBytesToHex(lastResponseBuild_.bytes, lastResponseBuild_.valid, PACKET_LEN);
    }

private:
    HardwareSerial* serial_;
    heatpumpSettings currentSettings_;
    heatpumpStatus currentStatus_;
    bool connected_ = false;
    String powerStorage_;
    String modeStorage_;
    String fanStorage_;
    String vaneStorage_;
    String wideVaneStorage_;
    FahrenheitSupport fahrenheitSupport_;
    cn105SetPacketBuild lastBuild_;
    cn105InfoResponseBuild lastResponseBuild_;
    uint8_t rxBuffer_[PACKET_LEN];
    int rxIndex_ = 0;
    int rxExpectedLength_ = 0;
    uint32_t lastRxByteMs_ = 0;

    void applyMockDefaults() {
        powerStorage_ = "ON";
        modeStorage_ = "AUTO";
        fanStorage_ = "AUTO";
        vaneStorage_ = "AUTO";
        wideVaneStorage_ = "AIRFLOW CONTROL";

        currentSettings_.temperature = fahrenheitSupport_.setpointFahrenheitToDeviceCelsius(AppConfig::DEFAULT_TARGET_TEMPERATURE_F);
        currentStatus_.roomTemperature = fahrenheitSupport_.setpointFahrenheitToDeviceCelsius(AppConfig::DEFAULT_ROOM_TEMPERATURE_F);
        currentStatus_.outsideAirTemperature = (AppConfig::DEFAULT_OUTSIDE_TEMPERATURE_F - 32.0f) / 1.8f;
        currentStatus_.compressorFrequency = 33.0f;
        currentStatus_.inputPower = 420.0f;
        currentStatus_.kWh = 12.4f;
        currentStatus_.runtimeHours = 87.5f;

        connected_ = true;
        currentSettings_.connected = true;
        syncSettingPointers();
        updateDerivedMockStatus();
        buildCurrentSetPacket();
        buildMockInfoResponsePacket(0x06, &lastResponseBuild_);
    }

    void resetRxState() {
        memset(rxBuffer_, 0, sizeof(rxBuffer_));
        rxIndex_ = 0;
        rxExpectedLength_ = 0;
        lastRxByteMs_ = 0;
    }

    static int expectedPacketLengthForCommand(uint8_t command) {
        if (command == 0x7A || command == 0x7B) {
            return CONNECT_LEN;
        }
        return PACKET_LEN;
    }

    void processIncomingByte(uint8_t value) {
        uint32_t now = millis();

        if (rxIndex_ > 0 && now - lastRxByteMs_ > AppConfig::CN105_RX_TIMEOUT_MS) {
            Serial.printf("[CN105 RX] Timeout after %lums, dropping partial packet (%d bytes)\n",
                          static_cast<unsigned long>(now - lastRxByteMs_),
                          rxIndex_);
            resetRxState();
        }

        lastRxByteMs_ = now;

        if (rxIndex_ == 0) {
            if (value != 0xFC) {
                return;
            }
            rxBuffer_[rxIndex_++] = value;
            return;
        }

        if (rxIndex_ >= PACKET_LEN) {
            Serial.println("[CN105 RX] Buffer overflow, resetting packet state");
            resetRxState();
            if (value == 0xFC) {
                rxBuffer_[rxIndex_++] = value;
                lastRxByteMs_ = now;
            }
            return;
        }

        rxBuffer_[rxIndex_++] = value;

        if (rxIndex_ == 2) {
            rxExpectedLength_ = expectedPacketLengthForCommand(rxBuffer_[1]);
        }

        if (rxExpectedLength_ > 0 && rxIndex_ >= rxExpectedLength_) {
            handleCompletedRxPacket(rxExpectedLength_);
            resetRxState();
        }
    }

    void handleCompletedRxPacket(int packetLength) {
        String packetHex = packetBytesToHex(rxBuffer_, true, packetLength);
        Serial.printf("[CN105 RX] %s\n", packetHex.c_str());

        if (packetLength == CONNECT_LEN) {
            Serial.printf("[CN105 RX] Short control packet: cmd=0x%02X\n", rxBuffer_[1]);
            return;
        }

        if (rxBuffer_[1] != 0x62) {
            Serial.printf("[CN105 RX] Unsupported packet command 0x%02X\n", rxBuffer_[1]);
            return;
        }

        String errorMessage;
        if (!decodeRawResponseHex(packetHex, &errorMessage)) {
            Serial.printf("[CN105 RX] Decode failed: %s\n", errorMessage.c_str());
            return;
        }

        Serial.printf("[CN105 RX] Applied INFO response 0x%02X\n", lastResponseBuild_.infoCode);
    }

    void syncSettingPointers() {
        currentSettings_.power = powerStorage_.c_str();
        currentSettings_.mode = modeStorage_.c_str();
        currentSettings_.fan = fanStorage_.c_str();
        currentSettings_.vane = vaneStorage_.c_str();
        currentSettings_.wideVane = wideVaneStorage_.c_str();
    }

    void updateDerivedMockStatus() {
        bool isOn = powerStorage_ == "ON";
        currentStatus_.operating = isOn;

        if (!isOn) {
            currentStatus_.compressorFrequency = 0.0f;
            currentStatus_.inputPower = 0.0f;
            return;
        }

        if (modeStorage_ == "HEAT") {
            currentStatus_.compressorFrequency = 38.0f;
            currentStatus_.inputPower = 510.0f;
        } else if (modeStorage_ == "COOL" || modeStorage_ == "DRY") {
            currentStatus_.compressorFrequency = 31.0f;
            currentStatus_.inputPower = 360.0f;
        } else if (modeStorage_ == "FAN") {
            currentStatus_.compressorFrequency = 12.0f;
            currentStatus_.inputPower = 85.0f;
        } else {
            currentStatus_.compressorFrequency = 24.0f;
            currentStatus_.inputPower = 250.0f;
        }
    }

    static uint8_t calcCheckSum(const uint8_t* bytes, int len) {
        uint8_t sum = 0;
        for (int i = 0; i < len; i++) {
            sum += bytes[i];
        }
        return (0xFC - sum) & 0xFF;
    }

    static String byteToHex(uint8_t value) {
        String out;
        if (value < 0x10) {
            out += "0";
        }
        out += String(value, HEX);
        out.toUpperCase();
        return out;
    }

    static int hexCharToNibble(char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return 10 + (c - 'a');
        }
        if (c >= 'A' && c <= 'F') {
            return 10 + (c - 'A');
        }
        return -1;
    }

    static bool parseHexByteToken(const String& token, uint8_t* outByte) {
        if (!outByte) {
            return false;
        }

        String normalized = token;
        normalized.trim();
        if (normalized.startsWith("0x") || normalized.startsWith("0X")) {
            normalized = normalized.substring(2);
        }

        if (normalized.length() != 2) {
            return false;
        }

        int high = hexCharToNibble(normalized[0]);
        int low = hexCharToNibble(normalized[1]);
        if (high < 0 || low < 0) {
            return false;
        }

        *outByte = static_cast<uint8_t>((high << 4) | low);
        return true;
    }

    static bool parseHexPacketString(const String& rawHex, uint8_t* outBytes, int expectedLen, String* errorMessage) {
        if (!outBytes || expectedLen <= 0) {
            if (errorMessage) {
                *errorMessage = "invalid output buffer";
            }
            return false;
        }

        String normalized = rawHex;
        normalized.trim();
        if (normalized.length() == 0) {
            if (errorMessage) {
                *errorMessage = "empty packet text";
            }
            return false;
        }

        String compact;
        compact.reserve(normalized.length());
        bool sawSeparator = false;

        for (size_t i = 0; i < normalized.length(); ++i) {
            char c = normalized[i];
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ',' || c == ';') {
                sawSeparator = true;
                continue;
            }
            compact += c;
        }

        int byteIndex = 0;
        if (!sawSeparator && !compact.startsWith("0x") && !compact.startsWith("0X")) {
            if ((compact.length() % 2) != 0) {
                if (errorMessage) {
                    *errorMessage = "continuous hex input must contain an even number of characters";
                }
                return false;
            }

            for (int i = 0; i < compact.length(); i += 2) {
                if (byteIndex >= expectedLen) {
                    if (errorMessage) {
                        *errorMessage = "too many bytes for one CN105 packet";
                    }
                    return false;
                }

                uint8_t value = 0;
                if (!parseHexByteToken(compact.substring(i, i + 2), &value)) {
                    if (errorMessage) {
                        *errorMessage = "invalid hex byte near position " + String(i);
                    }
                    return false;
                }
                outBytes[byteIndex++] = value;
            }
        } else {
            int start = 0;
            while (start < normalized.length()) {
                while (start < normalized.length()) {
                    char c = normalized[start];
                    if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ',' || c == ';') {
                        start++;
                    } else {
                        break;
                    }
                }
                if (start >= normalized.length()) {
                    break;
                }

                int end = start;
                while (end < normalized.length()) {
                    char c = normalized[end];
                    if (c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == ',' || c == ';') {
                        break;
                    }
                    end++;
                }

                if (byteIndex >= expectedLen) {
                    if (errorMessage) {
                        *errorMessage = "too many bytes for one CN105 packet";
                    }
                    return false;
                }

                uint8_t value = 0;
                String token = normalized.substring(start, end);
                if (!parseHexByteToken(token, &value)) {
                    if (errorMessage) {
                        *errorMessage = "invalid hex byte token: " + token;
                    }
                    return false;
                }

                outBytes[byteIndex++] = value;
                start = end + 1;
            }
        }

        if (byteIndex != expectedLen) {
            if (errorMessage) {
                if (!sawSeparator) {
                    *errorMessage = "expected " + String(expectedLen) + " bytes (" +
                                    String(expectedLen * 2) + " hex chars) but received " +
                                    String(byteIndex) + " bytes (" + String(compact.length()) +
                                    " hex chars)";
                } else {
                    *errorMessage = "expected " + String(expectedLen) + " bytes but received " + String(byteIndex);
                }
            }
            return false;
        }

        if (errorMessage) {
            *errorMessage = "";
        }
        return true;
    }

    static bool encodeLegacySetTemperatureByte(float tempC, uint8_t* outValue) {
        if (!outValue) {
            return false;
        }

        int wholeTemp = static_cast<int>(lroundf(tempC));
        if (fabsf(tempC - static_cast<float>(wholeTemp)) >= 0.01f) {
            return false;
        }

        for (int i = 0; i < 16; i++) {
            if (TEMP_MAP[i] == wholeTemp) {
                *outValue = TEMP[i];
                return true;
            }
        }

        return false;
    }

    static bool encodeLegacyRoomTemperatureByte(float tempC, uint8_t* outValue) {
        if (!outValue) {
            return false;
        }

        int wholeTemp = static_cast<int>(lroundf(tempC));
        if (fabsf(tempC - static_cast<float>(wholeTemp)) >= 0.01f) {
            return false;
        }

        for (int i = 0; i < 32; i++) {
            if (ROOM_TEMP_MAP[i] == wholeTemp) {
                *outValue = ROOM_TEMP[i];
                return true;
            }
        }

        return false;
    }

    static uint8_t encodeHighPrecisionTemperatureByte(float tempC) {
        int encoded = static_cast<int>(lroundf(tempC * 2.0f + 128.0f));
        if (encoded < 0) {
            encoded = 0;
        }
        if (encoded > 255) {
            encoded = 255;
        }
        return static_cast<uint8_t>(encoded);
    }

    static String packetBytesToHex(const uint8_t* bytes, bool valid, int len) {
        if (!valid || !bytes) {
            return "";
        }

        String out;
        for (int i = 0; i < len; i++) {
            if (i > 0) {
                out += " ";
            }
            if (bytes[i] < 0x10) {
                out += "0";
            }
            out += String(bytes[i], HEX);
        }
        out.toUpperCase();
        return out;
    }

    void buildCurrentSetPacket() {
        lastBuild_.reset();

        memcpy(lastBuild_.bytes, HEADER, HEADER_LEN);

        if (currentSettings_.power) {
            int idx = lookupByteMapIndex(POWER_MAP, 2, currentSettings_.power);
            if (idx >= 0) {
                lastBuild_.bytes[8] = POWER[idx];
                lastBuild_.bytes[6] |= CONTROL_PACKET_1[0];
            }
        }

        if (currentSettings_.mode) {
            int idx = lookupByteMapIndex(MODE_MAP, 5, currentSettings_.mode);
            if (idx >= 0) {
                lastBuild_.bytes[9] = MODE[idx];
                lastBuild_.bytes[6] |= CONTROL_PACKET_1[1];
            }
        }

        if (!isnan(currentSettings_.temperature) && currentSettings_.temperature > 0) {
            lastBuild_.encodedTemperatureC = currentSettings_.temperature;
            lastBuild_.usedHighPrecisionTemperature = true;

            // We prefer the high-precision temperature field at byte[19].
            // That keeps the builder consistent for values like 77F -> 25.0C and also
            // leaves room for finer-grained temperature handling later.
            //
            // Encoding formula:
            //   byte[19] = tempC * 2 + 128
            lastBuild_.bytes[19] = encodeHighPrecisionTemperatureByte(currentSettings_.temperature);

            uint8_t legacyTemperature = 0;
            if (encodeLegacySetTemperatureByte(currentSettings_.temperature, &legacyTemperature)) {
                // If the temperature is exactly a whole degree, we also fill the legacy byte[10] field.
                // This is not the primary path for half-degree support. It just makes the packet more complete
                // and closer to how some existing implementations populate both temperature fields.
                lastBuild_.bytes[10] = legacyTemperature;
            }

            lastBuild_.bytes[6] |= CONTROL_PACKET_1[2];
        }

        if (currentSettings_.fan) {
            int idx = lookupByteMapIndex(FAN_MAP, 6, currentSettings_.fan);
            if (idx >= 0) {
                lastBuild_.bytes[11] = FAN[idx];
                lastBuild_.bytes[6] |= CONTROL_PACKET_1[3];
            }
        }

        if (currentSettings_.vane) {
            int idx = lookupByteMapIndex(VANE_MAP, 7, currentSettings_.vane);
            if (idx >= 0) {
                lastBuild_.bytes[12] = VANE[idx];
                lastBuild_.bytes[6] |= CONTROL_PACKET_1[4];
            }
        }

        if (currentSettings_.wideVane) {
            int idx = lookupByteMapIndex(WIDEVANE_MAP, 8, currentSettings_.wideVane);
            if (idx >= 0) {
                lastBuild_.bytes[18] = WIDEVANE[idx];
                lastBuild_.bytes[7] |= CONTROL_PACKET_2[0];
            }
        }

        lastBuild_.control1 = lastBuild_.bytes[6];
        lastBuild_.control2 = lastBuild_.bytes[7];
        lastBuild_.bytes[21] = calcCheckSum(lastBuild_.bytes, 21);
        lastBuild_.valid = true;
    }

    bool buildMockInfoResponsePacket(uint8_t infoCode, cn105InfoResponseBuild* outBuild) const {
        if (!outBuild) {
            return false;
        }

        outBuild->reset();
        outBuild->bytes[0] = 0xFC;
        outBuild->bytes[1] = 0x62;
        outBuild->bytes[2] = 0x01;
        outBuild->bytes[3] = 0x30;
        outBuild->bytes[4] = 0x10;
        outBuild->infoCode = infoCode;

        uint8_t* payload = &outBuild->bytes[5];
        payload[0] = infoCode;

        if (infoCode == 0x02) {
            if (currentSettings_.power) {
                int idx = lookupByteMapIndex(POWER_MAP, 2, currentSettings_.power);
                if (idx >= 0) {
                    payload[3] = POWER[idx];
                }
            }

            if (currentSettings_.mode) {
                int idx = lookupByteMapIndex(MODE_MAP, 5, currentSettings_.mode);
                if (idx >= 0) {
                    payload[4] = MODE[idx] + (currentSettings_.iSee ? 0x08 : 0x00);
                }
            }

            if (!isnan(currentSettings_.temperature) && currentSettings_.temperature > 0) {
                uint8_t legacyTemperature = 0;
                if (encodeLegacySetTemperatureByte(currentSettings_.temperature, &legacyTemperature)) {
                    payload[5] = legacyTemperature;
                }
                payload[11] = encodeHighPrecisionTemperatureByte(currentSettings_.temperature);
            }

            if (currentSettings_.fan) {
                int idx = lookupByteMapIndex(FAN_MAP, 6, currentSettings_.fan);
                if (idx >= 0) {
                    payload[6] = FAN[idx];
                }
            }

            if (currentSettings_.vane) {
                int idx = lookupByteMapIndex(VANE_MAP, 7, currentSettings_.vane);
                if (idx >= 0) {
                    payload[7] = VANE[idx];
                }
            }

            if (currentSettings_.wideVane) {
                int idx = lookupByteMapIndex(WIDEVANE_MAP, 8, currentSettings_.wideVane);
                if (idx >= 0) {
                    payload[10] = WIDEVANE[idx];
                }
            }
        } else if (infoCode == 0x03) {
            if (!isnan(currentStatus_.roomTemperature)) {
                uint8_t legacyRoomTemperature = 0;
                if (encodeLegacyRoomTemperatureByte(currentStatus_.roomTemperature, &legacyRoomTemperature)) {
                    payload[3] = legacyRoomTemperature;
                }
                payload[6] = encodeHighPrecisionTemperatureByte(currentStatus_.roomTemperature);
            }

            if (!isnan(currentStatus_.outsideAirTemperature)) {
                payload[5] = encodeHighPrecisionTemperatureByte(currentStatus_.outsideAirTemperature);
            }

            uint32_t runtimeMinutes = 0;
            if (!isnan(currentStatus_.runtimeHours) && currentStatus_.runtimeHours > 0) {
                runtimeMinutes = static_cast<uint32_t>(lroundf(currentStatus_.runtimeHours * 60.0f));
            }

            payload[11] = static_cast<uint8_t>((runtimeMinutes >> 16) & 0xFF);
            payload[12] = static_cast<uint8_t>((runtimeMinutes >> 8) & 0xFF);
            payload[13] = static_cast<uint8_t>(runtimeMinutes & 0xFF);
        } else if (infoCode == 0x06) {
            int compressorFrequency = static_cast<int>(lroundf(currentStatus_.compressorFrequency));
            if (compressorFrequency < 0) {
                compressorFrequency = 0;
            }
            if (compressorFrequency > 255) {
                compressorFrequency = 255;
            }
            payload[3] = static_cast<uint8_t>(compressorFrequency);
            payload[4] = currentStatus_.operating ? 1 : 0;

            int inputPower = static_cast<int>(lroundf(currentStatus_.inputPower));
            if (inputPower < 0) {
                inputPower = 0;
            }
            if (inputPower > 65535) {
                inputPower = 65535;
            }
            payload[5] = static_cast<uint8_t>((inputPower >> 8) & 0xFF);
            payload[6] = static_cast<uint8_t>(inputPower & 0xFF);

            int energyTenths = static_cast<int>(lroundf(currentStatus_.kWh * 10.0f));
            if (energyTenths < 0) {
                energyTenths = 0;
            }
            if (energyTenths > 65535) {
                energyTenths = 65535;
            }
            payload[7] = static_cast<uint8_t>((energyTenths >> 8) & 0xFF);
            payload[8] = static_cast<uint8_t>(energyTenths & 0xFF);
        } else {
            return false;
        }

        outBuild->bytes[21] = calcCheckSum(outBuild->bytes, 21);
        outBuild->valid = true;
        return true;
    }

    bool parseInfoResponsePacket(const uint8_t* bytes, int len) {
        if (!bytes || len != PACKET_LEN) {
            return false;
        }

        if (bytes[0] != 0xFC || bytes[1] != 0x62 || bytes[4] != 0x10) {
            return false;
        }

        uint8_t checksum = calcCheckSum(bytes, len - 1);
        if (checksum != bytes[len - 1]) {
            Serial.printf("[CN105] Response checksum mismatch: expected %02X got %02X\n",
                          checksum, bytes[len - 1]);
            return false;
        }

        const uint8_t* data = &bytes[5];
        uint8_t infoCode = data[0];

        if (infoCode == 0x02) {
            parseSettingsResponse(data);
        } else if (infoCode == 0x03) {
            parseRoomTempResponse(data);
        } else if (infoCode == 0x06) {
            parseStatusResponse(data);
        } else {
            Serial.printf("[CN105] Unsupported mock info response code: 0x%02X\n", infoCode);
            return false;
        }

        return true;
    }

    void parseSettingsResponse(const uint8_t* data) {
        currentSettings_.connected = true;
        connected_ = true;

        powerStorage_ = lookupByteMapValue(POWER_MAP, POWER, 2, data[3]);

        currentSettings_.iSee = data[4] > 0x08;
        uint8_t modeValue = currentSettings_.iSee ? static_cast<uint8_t>(data[4] - 0x08) : data[4];
        modeStorage_ = lookupByteMapValue(MODE_MAP, MODE, 5, modeValue);

        if (data[11] != 0x00) {
            currentSettings_.temperature = static_cast<float>(data[11] - 128) / 2.0f;
        } else {
            currentSettings_.temperature =
                static_cast<float>(lookupByteMapValue(TEMP_MAP, TEMP, 16, data[5]));
        }

        fanStorage_ = lookupByteMapValue(FAN_MAP, FAN, 6, data[6]);
        vaneStorage_ = lookupByteMapValue(VANE_MAP, VANE, 7, data[7]);
        wideVaneStorage_ = lookupByteMapValue(WIDEVANE_MAP, WIDEVANE, 8, data[10] & 0x0F);

        syncSettingPointers();
        buildCurrentSetPacket();

        Serial.printf("[CN105] Parsed 0x02 settings: power=%s mode=%s temp=%.1fC fan=%s vane=%s wide=%s\n",
                      currentSettings_.power,
                      currentSettings_.mode,
                      currentSettings_.temperature,
                      currentSettings_.fan,
                      currentSettings_.vane,
                      currentSettings_.wideVane);
    }

    void parseRoomTempResponse(const uint8_t* data) {
        if (data[6] != 0x00) {
            currentStatus_.roomTemperature = static_cast<float>(data[6] - 128) / 2.0f;
        } else {
            currentStatus_.roomTemperature =
                static_cast<float>(lookupByteMapValue(ROOM_TEMP_MAP, ROOM_TEMP, 32, data[3]));
        }

        if (data[5] > 1) {
            currentStatus_.outsideAirTemperature = static_cast<float>(data[5] - 128) / 2.0f;
        } else {
            currentStatus_.outsideAirTemperature = NAN;
        }

        currentStatus_.runtimeHours =
            static_cast<float>((static_cast<uint32_t>(data[11]) << 16) |
                               (static_cast<uint32_t>(data[12]) << 8) |
                               static_cast<uint32_t>(data[13])) /
            60.0f;

        Serial.printf("[CN105] Parsed 0x03 temps: room=%.1fC outside=%.1fC runtime=%.1fh\n",
                      currentStatus_.roomTemperature,
                      currentStatus_.outsideAirTemperature,
                      currentStatus_.runtimeHours);
    }

    void parseStatusResponse(const uint8_t* data) {
        currentStatus_.compressorFrequency = static_cast<float>(data[3]);
        currentStatus_.operating = data[4] != 0;
        currentStatus_.inputPower =
            static_cast<float>((static_cast<uint16_t>(data[5]) << 8) | static_cast<uint16_t>(data[6]));
        currentStatus_.kWh =
            static_cast<float>((static_cast<uint16_t>(data[7]) << 8) | static_cast<uint16_t>(data[8])) / 10.0f;

        Serial.printf("[CN105] Parsed 0x06 status: compressor=%.0fHz operating=%s power=%.0fW energy=%.1fkWh\n",
                      currentStatus_.compressorFrequency,
                      currentStatus_.operating ? "YES" : "NO",
                      currentStatus_.inputPower,
                      currentStatus_.kWh);
    }
};
