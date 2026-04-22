#include "cn105_transport.h"

#include "app_config.h"
#include "cn105_core.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <cstring>

namespace {

const char* TAG = "cn105_transport";

enum class Phase {
    kDisconnected,
    kConnecting,
    kIdle,
    kAwaitingInfoResponse,
    kAwaitingSetAck,
};

struct TransportState {
    Phase phase = Phase::kDisconnected;
    bool taskRunning = false;
    bool connected = false;
    uint32_t connectAttempts = 0;
    uint32_t pollCycles = 0;
    uint32_t rxPackets = 0;
    uint32_t rxErrors = 0;
    uint32_t txPackets = 0;
    char lastError[96] = "";

    uint8_t rxBuf[cn105_core::kPacketLen] = {};
    size_t rxIndex = 0;
    size_t rxExpected = 0;
    int64_t rxLastByteUs = 0;

    uint8_t pollCodes[3] = {0x02, 0x03, 0x06};
    int pollIndex = 0;
    uint8_t awaitingInfoCode = 0;
    int64_t requestSentUs = 0;
    int64_t lastPollUs = 0;
    int64_t lastConnectAttemptUs = 0;
};

TransportState ts;
QueueHandle_t set_queue = nullptr;

const char* phaseName(Phase p) {
    switch (p) {
        case Phase::kDisconnected: return "disconnected";
        case Phase::kConnecting: return "connecting";
        case Phase::kIdle: return "idle";
        case Phase::kAwaitingInfoResponse: return "awaiting-info";
        case Phase::kAwaitingSetAck: return "awaiting-set-ack";
    }
    return "unknown";
}

int64_t nowUs() {
    return static_cast<int64_t>(esp_timer_get_time());
}

bool elapsedMs(int64_t since_us, uint32_t ms) {
    return (nowUs() - since_us) >= static_cast<int64_t>(ms) * 1000;
}

void setError(const char* msg) {
    std::strncpy(ts.lastError, msg, sizeof(ts.lastError) - 1);
    ts.lastError[sizeof(ts.lastError) - 1] = '\0';
}

bool uartSend(const uint8_t* data, size_t len) {
    const int written = uart_write_bytes(app_config::kCn105UartPort, data, len);
    if (written < 0 || static_cast<size_t>(written) != len) {
        setError("uart_write_bytes failed");
        return false;
    }
    ts.txPackets++;
    return true;
}

void sendConnect() {
    cn105_core::Packet packet{};
    char error[96] = {};
    if (!cn105_core::buildConnectPacket(&packet, error, sizeof(error))) {
        setError(error);
        return;
    }
    if (uartSend(packet.bytes, packet.length)) {
        ts.phase = Phase::kConnecting;
        ts.requestSentUs = nowUs();
        ts.connectAttempts++;
        ESP_LOGI(TAG, "CONNECT sent (attempt %lu)", static_cast<unsigned long>(ts.connectAttempts));
    }
}

void sendInfoRequest(uint8_t code) {
    cn105_core::Packet packet{};
    char error[96] = {};
    if (!cn105_core::buildInfoPacket(code, &packet, error, sizeof(error))) {
        setError(error);
        return;
    }
    if (uartSend(packet.bytes, packet.length)) {
        ts.phase = Phase::kAwaitingInfoResponse;
        ts.awaitingInfoCode = code;
        ts.requestSentUs = nowUs();
    }
}

void sendSetPacket(const cn105_core::SetCommand& command) {
    cn105_core::Packet packet{};
    char error[96] = {};
    if (!cn105_core::buildSetPacket(command, &packet, error, sizeof(error))) {
        setError(error);
        ESP_LOGW(TAG, "SET build failed: %s", error);
        return;
    }
    if (uartSend(packet.bytes, packet.length)) {
        ts.phase = Phase::kAwaitingSetAck;
        ts.requestSentUs = nowUs();
        ESP_LOGI(TAG, "SET sent");
    }
}

size_t expectedLenForCommand(uint8_t command) {
    // CONNECT ACK packets can be 7 bytes long (payload length 0x01), so ACKs
    // must be parsed using the standard "payload length + 6" framing logic.
    (void) command;
    return 0;
}

void resetRx() {
    ts.rxIndex = 0;
    ts.rxExpected = 0;
}

void handlePacket(const uint8_t* bytes, size_t len) {
    ts.rxPackets++;

    const uint8_t cmd = bytes[1];
    const uint8_t expected_checksum = cn105_core::checksum(bytes, len - 1);
    if (bytes[len - 1] != expected_checksum) {
        ts.rxErrors++;
        setError("rx checksum mismatch");
        ESP_LOGW(TAG, "RX checksum mismatch: got=%02X expected=%02X", bytes[len - 1], expected_checksum);
        return;
    }

    switch (cmd) {
        case 0x7A:
        case 0x7B:
            if (ts.phase == Phase::kConnecting) {
                ts.connected = true;
                ts.phase = Phase::kIdle;
                ts.lastPollUs = nowUs();
                cn105_core::setConnected(true);
                setError("");
                ESP_LOGI(TAG, "CONNECT acknowledged (0x%02X)", cmd);
            }
            break;
        case 0x62:
            if (ts.phase == Phase::kAwaitingInfoResponse) {
                cn105_core::applyInfoResponseToState(bytes, len);
                ts.phase = Phase::kIdle;
            }
            break;
        case 0x61:
            if (ts.phase == Phase::kAwaitingSetAck) {
                ts.phase = Phase::kIdle;
                ESP_LOGI(TAG, "SET acknowledged");
            }
            break;
        default:
            ESP_LOGD(TAG, "RX unexpected command 0x%02X", cmd);
            break;
    }
}

void processRxByte(uint8_t byte) {
    if (ts.rxIndex == 0 && byte != 0xFC) {
        return;
    }

    ts.rxBuf[ts.rxIndex++] = byte;
    ts.rxLastByteUs = nowUs();

    if (ts.rxIndex == 2) {
        const size_t short_len = expectedLenForCommand(byte);
        if (short_len > 0) {
            ts.rxExpected = short_len;
        }
    }

    if (ts.rxIndex == 5 && ts.rxExpected == 0) {
        ts.rxExpected = static_cast<size_t>(ts.rxBuf[4]) + 6;
        if (ts.rxExpected > cn105_core::kPacketLen) {
            ESP_LOGW(TAG, "RX expected length %u exceeds max, resetting", static_cast<unsigned>(ts.rxExpected));
            ts.rxErrors++;
            resetRx();
            return;
        }
    }

    if (ts.rxExpected > 0 && ts.rxIndex >= ts.rxExpected) {
        handlePacket(ts.rxBuf, ts.rxIndex);
        resetRx();
    }

    if (ts.rxIndex >= cn105_core::kPacketLen) {
        ts.rxErrors++;
        resetRx();
    }
}

void drainUart() {
    uint8_t byte;
    while (uart_read_bytes(app_config::kCn105UartPort, &byte, 1, 0) == 1) {
        processRxByte(byte);
    }
}

void checkRxTimeout() {
    if (ts.rxIndex > 0 && elapsedMs(ts.rxLastByteUs, app_config::kCn105RxByteTimeoutMs)) {
        ESP_LOGD(TAG, "RX timeout after %u bytes, resetting", static_cast<unsigned>(ts.rxIndex));
        ts.rxErrors++;
        resetRx();
    }
}

void checkResponseTimeout() {
    if (ts.phase == Phase::kConnecting && elapsedMs(ts.requestSentUs, app_config::kCn105ResponseTimeoutMs)) {
        ESP_LOGW(TAG, "CONNECT response timeout");
        ts.phase = Phase::kDisconnected;
        setError("connect timeout");
    }
    if (ts.phase == Phase::kAwaitingInfoResponse && elapsedMs(ts.requestSentUs, app_config::kCn105ResponseTimeoutMs)) {
        ESP_LOGW(TAG, "INFO 0x%02X response timeout", ts.awaitingInfoCode);
        ts.phase = Phase::kIdle;
        setError("info response timeout");
    }
    if (ts.phase == Phase::kAwaitingSetAck && elapsedMs(ts.requestSentUs, app_config::kCn105ResponseTimeoutMs)) {
        ESP_LOGW(TAG, "SET ACK timeout");
        ts.phase = Phase::kIdle;
        setError("set ack timeout");
    }
}

void transportTask(void*) {
    ts.taskRunning = true;
    ESP_LOGI(TAG, "Transport task started");

    while (true) {
        drainUart();
        checkRxTimeout();
        checkResponseTimeout();

        switch (ts.phase) {
            case Phase::kDisconnected:
                if (elapsedMs(ts.lastConnectAttemptUs, app_config::kCn105ConnectRetryMs)) {
                    ts.lastConnectAttemptUs = nowUs();
                    sendConnect();
                }
                break;

            case Phase::kIdle: {
                cn105_core::SetCommand command{};
                if (set_queue && xQueueReceive(set_queue, &command, 0) == pdTRUE) {
                    sendSetPacket(command);
                    break;
                }
                if (elapsedMs(ts.lastPollUs, app_config::kCn105PollIntervalMs)) {
                    sendInfoRequest(ts.pollCodes[ts.pollIndex]);
                    ts.pollIndex = (ts.pollIndex + 1) % 3;
                    if (ts.pollIndex == 0) {
                        ts.pollCycles++;
                    }
                    ts.lastPollUs = nowUs();
                }
                break;
            }

            case Phase::kConnecting:
            case Phase::kAwaitingInfoResponse:
            case Phase::kAwaitingSetAck:
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

}  // namespace

namespace cn105_transport {

esp_err_t start() {
    if (ts.taskRunning) {
        return ESP_OK;
    }

    set_queue = xQueueCreate(2, sizeof(cn105_core::SetCommand));
    if (set_queue == nullptr) {
        ESP_LOGE(TAG, "Failed to create SET command queue");
        return ESP_FAIL;
    }

    const BaseType_t ret = xTaskCreate(transportTask, "cn105_tx", app_config::kCn105TransportStackBytes, nullptr, 5, nullptr);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create transport task");
        return ESP_FAIL;
    }

    return ESP_OK;
}

Status getStatus() {
    Status status{};
    status.taskRunning = ts.taskRunning;
    status.connected = ts.connected;
    status.connectAttempts = ts.connectAttempts;
    status.pollCycles = ts.pollCycles;
    status.rxPackets = ts.rxPackets;
    status.rxErrors = ts.rxErrors;
    status.txPackets = ts.txPackets;
    status.setsPending = set_queue ? static_cast<uint32_t>(uxQueueMessagesWaiting(set_queue)) : 0;
    std::strncpy(status.phase, phaseName(ts.phase), sizeof(status.phase) - 1);
    std::strncpy(status.lastError, ts.lastError, sizeof(status.lastError) - 1);
    return status;
}

bool queueSetCommand(const cn105_core::SetCommand& command) {
    if (set_queue == nullptr) {
        return false;
    }
    return xQueueSend(set_queue, &command, pdMS_TO_TICKS(100)) == pdTRUE;
}

}  // namespace cn105_transport
