#include "homekit_bridge.h"

#include "app_config.h"
#include "cn105_core.h"
#include "esp_event.h"
#include "esp_log.h"

extern "C" {
#include "hap.h"
#include "hap_apple_chars.h"
#include "hap_apple_servs.h"
}

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

const char* TAG = "homekit_bridge";

constexpr uint8_t kCurrentOff = 0;
constexpr uint8_t kCurrentHeat = 1;
constexpr uint8_t kCurrentCool = 2;
constexpr uint8_t kTargetOff = 0;
constexpr uint8_t kTargetHeat = 1;
constexpr uint8_t kTargetCool = 2;
constexpr uint8_t kTargetAuto = 3;
constexpr uint8_t kDisplayFahrenheit = 1;
constexpr float kMinTargetCelsius = 16.0f;
constexpr float kMaxTargetCelsius = 31.0f;
constexpr float kTargetStepCelsius = 0.5f;
constexpr uint8_t kInactive = 0;
constexpr uint8_t kActive = 1;
constexpr uint8_t kFanStateInactive = 0;
constexpr uint8_t kFanStateBlowing = 2;
constexpr uint8_t kTargetFanManual = 0;
constexpr uint8_t kTargetFanAuto = 1;
constexpr uint8_t kSwingDisabled = 0;
constexpr uint8_t kSwingEnabled = 1;
constexpr uint8_t kSlatFixed = 0;
constexpr uint8_t kSlatSwinging = 2;
constexpr uint8_t kSlatTypeHorizontal = 0;
constexpr uint8_t kSlatTypeVertical = 1;

bool started = false;
hap_acc_t* accessory = nullptr;
hap_serv_t* thermostat = nullptr;
hap_serv_t* fan = nullptr;
hap_serv_t* vertical_vane_slat = nullptr;
hap_serv_t* wide_vane_slat = nullptr;
hap_char_t* current_state_char = nullptr;
hap_char_t* target_state_char = nullptr;
hap_char_t* current_temp_char = nullptr;
hap_char_t* target_temp_char = nullptr;
hap_char_t* temp_units_char = nullptr;
hap_char_t* fan_active_char = nullptr;
hap_char_t* fan_speed_char = nullptr;
hap_char_t* fan_current_state_char = nullptr;
hap_char_t* fan_target_state_char = nullptr;
hap_char_t* fan_swing_char = nullptr;
hap_char_t* vertical_slat_state_char = nullptr;
hap_char_t* vertical_current_tilt_char = nullptr;
hap_char_t* vertical_target_tilt_char = nullptr;
hap_char_t* wide_slat_state_char = nullptr;
hap_char_t* wide_current_tilt_char = nullptr;
hap_char_t* wide_target_tilt_char = nullptr;
char setup_payload[128] = "";
char last_event[40] = "not-started";
char last_error[96] = "";

char* hapString(const char* value) {
    return const_cast<char*>(value);
}

float fahrenheitToCelsius(int value_f) {
    return (static_cast<float>(value_f) - 32.0f) * 5.0f / 9.0f;
}

int celsiusToRoundedFahrenheit(float value_c) {
    const float value_f = (value_c * 9.0f / 5.0f) + 32.0f;
    return static_cast<int>(value_f + 0.5f);
}

int clampFahrenheit(int value_f) {
    if (value_f < 61) {
        return 61;
    }
    if (value_f > 88) {
        return 88;
    }
    return value_f;
}

bool equals(const char* left, const char* right) {
    return std::strcmp(left, right) == 0;
}

uint8_t targetStateFromMock(const cn105_core::MockState& state) {
    if (equals(state.power, "OFF")) {
        return kTargetOff;
    }
    if (equals(state.mode, "HEAT")) {
        return kTargetHeat;
    }
    if (equals(state.mode, "COOL")) {
        return kTargetCool;
    }
    return kTargetAuto;
}

uint8_t currentStateFromMock(const cn105_core::MockState& state) {
    if (equals(state.power, "OFF") || !state.operating) {
        return kCurrentOff;
    }
    if (equals(state.mode, "HEAT")) {
        return kCurrentHeat;
    }
    if (equals(state.mode, "COOL")) {
        return kCurrentCool;
    }
    return kCurrentOff;
}

const char* modeFromHomeKitTarget(uint8_t target_state) {
    switch (target_state) {
        case kTargetHeat:
            return "HEAT";
        case kTargetCool:
            return "COOL";
        case kTargetAuto:
            return "AUTO";
        default:
            return nullptr;
    }
}

uint8_t activeFromMock(const cn105_core::MockState& state) {
    return equals(state.power, "ON") ? kActive : kInactive;
}

uint8_t currentFanStateFromMock(const cn105_core::MockState& state) {
    return equals(state.power, "ON") ? kFanStateBlowing : kFanStateInactive;
}

uint8_t targetFanStateFromMock(const cn105_core::MockState& state) {
    return equals(state.fan, "AUTO") ? kTargetFanAuto : kTargetFanManual;
}

float fanSpeedFromMock(const cn105_core::MockState& state) {
    if (equals(state.fan, "QUIET")) {
        return 15.0f;
    }
    if (equals(state.fan, "1")) {
        return 25.0f;
    }
    if (equals(state.fan, "2")) {
        return 45.0f;
    }
    if (equals(state.fan, "3")) {
        return 65.0f;
    }
    if (equals(state.fan, "4")) {
        return 85.0f;
    }
    return 50.0f;
}

const char* fanFromSpeed(float speed) {
    if (speed <= 20.0f) {
        return "QUIET";
    }
    if (speed <= 35.0f) {
        return "1";
    }
    if (speed <= 55.0f) {
        return "2";
    }
    if (speed <= 75.0f) {
        return "3";
    }
    return "4";
}

uint8_t swingFromMock(const cn105_core::MockState& state) {
    return equals(state.vane, "SWING") || equals(state.wideVane, "SWING") ? kSwingEnabled : kSwingDisabled;
}

uint8_t slatStateFromValue(const char* value) {
    return equals(value, "SWING") ? kSlatSwinging : kSlatFixed;
}

int verticalTiltFromVane(const char* vane) {
    if (equals(vane, "1")) {
        return -60;
    }
    if (equals(vane, "2")) {
        return -30;
    }
    if (equals(vane, "4")) {
        return 30;
    }
    if (equals(vane, "5")) {
        return 60;
    }
    return 0;
}

int horizontalTiltFromWideVane(const char* wide_vane) {
    if (equals(wide_vane, "<<")) {
        return -60;
    }
    if (equals(wide_vane, "<")) {
        return -30;
    }
    if (equals(wide_vane, ">")) {
        return 30;
    }
    if (equals(wide_vane, ">>")) {
        return 60;
    }
    return 0;
}

const char* vaneFromVerticalTilt(int angle) {
    if (angle <= -45) {
        return "1";
    }
    if (angle <= -15) {
        return "2";
    }
    if (angle <= 15) {
        return "3";
    }
    if (angle <= 45) {
        return "4";
    }
    return "5";
}

const char* wideVaneFromHorizontalTilt(int angle) {
    if (angle <= -45) {
        return "<<";
    }
    if (angle <= -15) {
        return "<";
    }
    if (angle <= 15) {
        return "|";
    }
    if (angle <= 45) {
        return ">";
    }
    return ">>";
}

void setLastEvent(const char* value) {
    std::strncpy(last_event, value, sizeof(last_event) - 1);
    last_event[sizeof(last_event) - 1] = '\0';
}

void setLastError(const char* value) {
    std::strncpy(last_error, value, sizeof(last_error) - 1);
    last_error[sizeof(last_error) - 1] = '\0';
}

void updateCharUInt8(hap_char_t* character, uint8_t value) {
    if (character == nullptr) {
        return;
    }
    hap_val_t hap_value = {};
    hap_value.u = value;
    hap_char_update_val(character, &hap_value);
}

void updateCharFloat(hap_char_t* character, float value) {
    if (character == nullptr) {
        return;
    }
    hap_val_t hap_value = {};
    hap_value.f = value;
    hap_char_update_val(character, &hap_value);
}

void updateCharInt(hap_char_t* character, int value) {
    if (character == nullptr) {
        return;
    }
    hap_val_t hap_value = {};
    hap_value.i = value;
    hap_char_update_val(character, &hap_value);
}

bool applyToMock(const cn105_core::SetCommand& command) {
    cn105_core::Packet packet{};
    char error[96] = {};
    if (!cn105_core::buildSetPacket(command, &packet, error, sizeof(error))) {
        setLastError(error);
        ESP_LOGW(TAG, "HomeKit command build failed: %s", error);
        return false;
    }
    if (!cn105_core::applySetPacketToMock(packet.bytes, packet.length, error, sizeof(error))) {
        setLastError(error);
        ESP_LOGW(TAG, "HomeKit command mock apply failed: %s", error);
        return false;
    }

    setLastError("");
    return true;
}

int identify(hap_acc_t*) {
    ESP_LOGI(TAG, "Identify requested");
    setLastEvent("identify");
    return HAP_SUCCESS;
}

void hapEventHandler(void*, esp_event_base_t, int32_t event_id, void*) {
    switch (event_id) {
        case HAP_EVENT_CTRL_PAIRED:
            setLastEvent("controller-paired");
            break;
        case HAP_EVENT_CTRL_UNPAIRED:
            setLastEvent("controller-unpaired");
            break;
        case HAP_EVENT_CTRL_CONNECTED:
            setLastEvent("controller-connected");
            break;
        case HAP_EVENT_CTRL_DISCONNECTED:
            setLastEvent("controller-disconnected");
            break;
        case HAP_EVENT_PAIRING_STARTED:
            setLastEvent("pairing-started");
            break;
        case HAP_EVENT_PAIRING_ABORTED:
            setLastEvent("pairing-aborted");
            break;
        case HAP_EVENT_GET_ACC_COMPLETED:
            setLastEvent("get-accessories");
            break;
        case HAP_EVENT_GET_CHAR_COMPLETED:
            setLastEvent("get-characteristics");
            break;
        case HAP_EVENT_SET_CHAR_COMPLETED:
            setLastEvent("set-characteristics");
            break;
        case HAP_EVENT_PAIRING_MODE_TIMED_OUT:
            setLastEvent("pairing-timeout");
            break;
        default:
            std::snprintf(last_event, sizeof(last_event), "event-%ld", static_cast<long>(event_id));
            break;
    }
}

int thermostatWrite(hap_write_data_t write_data[], int count, void*, void*) {
    cn105_core::SetCommand command{};
    bool should_apply = false;

    for (int i = 0; i < count; ++i) {
        if (write_data[i].hc == target_state_char) {
            const uint8_t target_state = static_cast<uint8_t>(write_data[i].val.u);
            command.hasPower = true;
            command.power = target_state == kTargetOff ? "OFF" : "ON";

            const char* mode = modeFromHomeKitTarget(target_state);
            if (mode != nullptr) {
                command.hasMode = true;
                command.mode = mode;
            }
            should_apply = true;
        } else if (write_data[i].hc == target_temp_char) {
            command.hasTemperatureF = true;
            command.temperatureF = clampFahrenheit(celsiusToRoundedFahrenheit(write_data[i].val.f));
            should_apply = true;
        } else if (write_data[i].hc == temp_units_char) {
            updateCharUInt8(temp_units_char, kDisplayFahrenheit);
        }

        if (write_data[i].status != nullptr) {
            *(write_data[i].status) = HAP_STATUS_SUCCESS;
        }
    }

    if (should_apply && !applyToMock(command)) {
        for (int i = 0; i < count; ++i) {
            if (write_data[i].status != nullptr) {
                *(write_data[i].status) = HAP_STATUS_RES_ABSENT;
            }
        }
        return HAP_FAIL;
    }

    homekit_bridge::syncFromMock();
    setLastEvent("write-applied");
    return HAP_SUCCESS;
}

int fanWrite(hap_write_data_t write_data[], int count, void*, void*) {
    cn105_core::SetCommand command{};
    bool should_apply = false;

    for (int i = 0; i < count; ++i) {
        if (write_data[i].hc == fan_active_char) {
            command.hasPower = true;
            command.power = write_data[i].val.u == kActive ? "ON" : "OFF";
            should_apply = true;
        } else if (write_data[i].hc == fan_speed_char) {
            command.hasFan = true;
            command.fan = fanFromSpeed(write_data[i].val.f);
            should_apply = true;
        } else if (write_data[i].hc == fan_target_state_char) {
            if (write_data[i].val.u == kTargetFanAuto) {
                command.hasFan = true;
                command.fan = "AUTO";
                should_apply = true;
            }
        } else if (write_data[i].hc == fan_swing_char) {
            command.hasVane = true;
            command.hasWideVane = true;
            if (write_data[i].val.u == kSwingEnabled) {
                command.vane = "SWING";
                command.wideVane = "SWING";
            } else {
                command.vane = "AUTO";
                command.wideVane = "|";
            }
            should_apply = true;
        }

        if (write_data[i].status != nullptr) {
            *(write_data[i].status) = HAP_STATUS_SUCCESS;
        }
    }

    if (should_apply && !applyToMock(command)) {
        for (int i = 0; i < count; ++i) {
            if (write_data[i].status != nullptr) {
                *(write_data[i].status) = HAP_STATUS_RES_ABSENT;
            }
        }
        return HAP_FAIL;
    }

    homekit_bridge::syncFromMock();
    setLastEvent("fan-write-applied");
    return HAP_SUCCESS;
}

int slatWrite(hap_write_data_t write_data[], int count, void*, void*) {
    cn105_core::SetCommand command{};
    bool should_apply = false;

    for (int i = 0; i < count; ++i) {
        if (write_data[i].hc == vertical_target_tilt_char) {
            command.hasVane = true;
            command.vane = vaneFromVerticalTilt(write_data[i].val.i);
            should_apply = true;
        } else if (write_data[i].hc == wide_target_tilt_char) {
            command.hasWideVane = true;
            command.wideVane = wideVaneFromHorizontalTilt(write_data[i].val.i);
            should_apply = true;
        }

        if (write_data[i].status != nullptr) {
            *(write_data[i].status) = HAP_STATUS_SUCCESS;
        }
    }

    if (should_apply && !applyToMock(command)) {
        for (int i = 0; i < count; ++i) {
            if (write_data[i].status != nullptr) {
                *(write_data[i].status) = HAP_STATUS_RES_ABSENT;
            }
        }
        return HAP_FAIL;
    }

    homekit_bridge::syncFromMock();
    setLastEvent("slat-write-applied");
    return HAP_SUCCESS;
}

esp_err_t addThermostatService(hap_acc_t* target_accessory) {
    const cn105_core::MockState state = cn105_core::getMockState();
    thermostat = hap_serv_thermostat_create(currentStateFromMock(state),
                                            targetStateFromMock(state),
                                            fahrenheitToCelsius(state.roomTemperatureF),
                                            fahrenheitToCelsius(state.targetTemperatureF),
                                            kDisplayFahrenheit);
    if (thermostat == nullptr) {
        setLastError("failed to create thermostat service");
        return ESP_FAIL;
    }

    int ret = hap_serv_add_char(thermostat, hap_char_name_create(hapString(app_config::kHomeKitAccessoryName)));
    if (ret != HAP_SUCCESS) {
        setLastError("failed to add thermostat name");
        return ESP_FAIL;
    }

    hap_serv_set_write_cb(thermostat, thermostatWrite);
    hap_acc_add_serv(target_accessory, thermostat);

    current_state_char = hap_serv_get_char_by_uuid(thermostat, HAP_CHAR_UUID_CURRENT_HEATING_COOLING_STATE);
    target_state_char = hap_serv_get_char_by_uuid(thermostat, HAP_CHAR_UUID_TARGET_HEATING_COOLING_STATE);
    current_temp_char = hap_serv_get_char_by_uuid(thermostat, HAP_CHAR_UUID_CURRENT_TEMPERATURE);
    target_temp_char = hap_serv_get_char_by_uuid(thermostat, HAP_CHAR_UUID_TARGET_TEMPERATURE);
    temp_units_char = hap_serv_get_char_by_uuid(thermostat, HAP_CHAR_UUID_TEMPERATURE_DISPLAY_UNITS);

    if (current_state_char == nullptr || target_state_char == nullptr || current_temp_char == nullptr ||
        target_temp_char == nullptr || temp_units_char == nullptr) {
        setLastError("thermostat characteristic lookup failed");
        return ESP_FAIL;
    }

    hap_char_float_set_constraints(target_temp_char, kMinTargetCelsius, kMaxTargetCelsius, kTargetStepCelsius);
    return ESP_OK;
}

esp_err_t addFanService(hap_acc_t* target_accessory) {
    const cn105_core::MockState state = cn105_core::getMockState();
    fan = hap_serv_fan_v2_create(activeFromMock(state));
    if (fan == nullptr) {
        setLastError("failed to create fan service");
        return ESP_FAIL;
    }

    int ret = hap_serv_add_char(fan, hap_char_name_create(hapString("Mitsubishi AC Fan")));
    ret |= hap_serv_add_char(fan, hap_char_rotation_speed_create(fanSpeedFromMock(state)));
    ret |= hap_serv_add_char(fan, hap_char_current_fan_state_create(currentFanStateFromMock(state)));
    ret |= hap_serv_add_char(fan, hap_char_target_fan_state_create(targetFanStateFromMock(state)));
    ret |= hap_serv_add_char(fan, hap_char_swing_mode_create(swingFromMock(state)));
    if (ret != HAP_SUCCESS) {
        setLastError("failed to add fan characteristics");
        return ESP_FAIL;
    }

    hap_serv_set_write_cb(fan, fanWrite);
    hap_acc_add_serv(target_accessory, fan);

    fan_active_char = hap_serv_get_char_by_uuid(fan, HAP_CHAR_UUID_ACTIVE);
    fan_speed_char = hap_serv_get_char_by_uuid(fan, HAP_CHAR_UUID_ROTATION_SPEED);
    fan_current_state_char = hap_serv_get_char_by_uuid(fan, HAP_CHAR_UUID_CURRENT_FAN_STATE);
    fan_target_state_char = hap_serv_get_char_by_uuid(fan, HAP_CHAR_UUID_TARGET_FAN_STATE);
    fan_swing_char = hap_serv_get_char_by_uuid(fan, HAP_CHAR_UUID_SWING_MODE);

    if (fan_active_char == nullptr || fan_speed_char == nullptr || fan_current_state_char == nullptr ||
        fan_target_state_char == nullptr || fan_swing_char == nullptr) {
        setLastError("fan characteristic lookup failed");
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t addSlatServices(hap_acc_t* target_accessory) {
    const cn105_core::MockState state = cn105_core::getMockState();

    vertical_vane_slat = hap_serv_slat_create(slatStateFromValue(state.vane), kSlatTypeVertical);
    if (vertical_vane_slat == nullptr) {
        setLastError("failed to create vertical vane slat service");
        return ESP_FAIL;
    }

    int ret = hap_serv_add_char(vertical_vane_slat, hap_char_name_create(hapString("Mitsubishi AC Vertical Vane")));
    ret |= hap_serv_add_char(vertical_vane_slat, hap_char_current_tilt_angle_create(verticalTiltFromVane(state.vane)));
    ret |= hap_serv_add_char(vertical_vane_slat, hap_char_target_tilt_angle_create(verticalTiltFromVane(state.vane)));
    if (ret != HAP_SUCCESS) {
        setLastError("failed to add vertical vane characteristics");
        return ESP_FAIL;
    }

    hap_serv_set_write_cb(vertical_vane_slat, slatWrite);
    hap_acc_add_serv(target_accessory, vertical_vane_slat);

    wide_vane_slat = hap_serv_slat_create(slatStateFromValue(state.wideVane), kSlatTypeHorizontal);
    if (wide_vane_slat == nullptr) {
        setLastError("failed to create wide vane slat service");
        return ESP_FAIL;
    }

    ret = hap_serv_add_char(wide_vane_slat, hap_char_name_create(hapString("Mitsubishi AC Horizontal Vane")));
    ret |= hap_serv_add_char(wide_vane_slat, hap_char_current_tilt_angle_create(horizontalTiltFromWideVane(state.wideVane)));
    ret |= hap_serv_add_char(wide_vane_slat, hap_char_target_tilt_angle_create(horizontalTiltFromWideVane(state.wideVane)));
    if (ret != HAP_SUCCESS) {
        setLastError("failed to add wide vane characteristics");
        return ESP_FAIL;
    }

    hap_serv_set_write_cb(wide_vane_slat, slatWrite);
    hap_acc_add_serv(target_accessory, wide_vane_slat);

    vertical_slat_state_char = hap_serv_get_char_by_uuid(vertical_vane_slat, HAP_CHAR_UUID_CURRENT_SLAT_STATE);
    vertical_current_tilt_char = hap_serv_get_char_by_uuid(vertical_vane_slat, HAP_CHAR_UUID_CURRENT_TILT_ANGLE);
    vertical_target_tilt_char = hap_serv_get_char_by_uuid(vertical_vane_slat, HAP_CHAR_UUID_TARGET_TILT_ANGLE);
    wide_slat_state_char = hap_serv_get_char_by_uuid(wide_vane_slat, HAP_CHAR_UUID_CURRENT_SLAT_STATE);
    wide_current_tilt_char = hap_serv_get_char_by_uuid(wide_vane_slat, HAP_CHAR_UUID_CURRENT_TILT_ANGLE);
    wide_target_tilt_char = hap_serv_get_char_by_uuid(wide_vane_slat, HAP_CHAR_UUID_TARGET_TILT_ANGLE);

    if (vertical_slat_state_char == nullptr || vertical_current_tilt_char == nullptr ||
        vertical_target_tilt_char == nullptr || wide_slat_state_char == nullptr || wide_current_tilt_char == nullptr ||
        wide_target_tilt_char == nullptr) {
        setLastError("slat characteristic lookup failed");
        return ESP_FAIL;
    }

    if (fan != nullptr) {
        int link_ret = hap_serv_link_serv(fan, vertical_vane_slat);
        link_ret |= hap_serv_link_serv(fan, wide_vane_slat);
        if (link_ret != HAP_SUCCESS) {
            setLastError("failed to link slat services to fan service");
            return ESP_FAIL;
        }
    }

    return ESP_OK;
}

}  // namespace

namespace homekit_bridge {

esp_err_t start() {
    if (!app_config::kHomeKitEnabled) {
        setLastEvent("disabled");
        ESP_LOGI(TAG, "HomeKit disabled by app_config");
        return ESP_OK;
    }
    if (started) {
        return ESP_OK;
    }

    setLastEvent("starting");
    setLastError("");

    if (hap_init(HAP_TRANSPORT_WIFI) != HAP_SUCCESS) {
        setLastError("hap_init failed");
        return ESP_FAIL;
    }

    hap_acc_cfg_t cfg = {
        .name = hapString(app_config::kHomeKitAccessoryName),
        .model = hapString(app_config::kHomeKitModel),
        .manufacturer = hapString(app_config::kHomeKitManufacturer),
        .serial_num = hapString(app_config::kHomeKitSerialNumber),
        .fw_rev = hapString(app_config::kHomeKitFirmwareRevision),
        .hw_rev = hapString(app_config::kHomeKitHardwareRevision),
        .pv = hapString("1.1.0"),
        .cid = HAP_CID_THERMOSTAT,
        .identify_routine = identify,
    };

    accessory = hap_acc_create(&cfg);
    if (accessory == nullptr) {
        setLastError("hap_acc_create failed");
        return ESP_FAIL;
    }

    uint8_t product_data[] = {'D', 'K', 'T', 'M', 'I', 'T', 'S', 'U'};
    hap_acc_add_product_data(accessory, product_data, sizeof(product_data));
    hap_acc_add_wifi_transport_service(accessory, 0);

    esp_err_t err = addThermostatService(accessory);
    if (err != ESP_OK) {
        return err;
    }
    err = addFanService(accessory);
    if (err != ESP_OK) {
        return err;
    }
    err = addSlatServices(accessory);
    if (err != ESP_OK) {
        return err;
    }

    hap_add_accessory(accessory);
    hap_set_setup_code(app_config::kHomeKitSetupCode);
    if (hap_set_setup_id(app_config::kHomeKitSetupId) != HAP_SUCCESS) {
        setLastError("hap_set_setup_id failed");
        return ESP_FAIL;
    }

    char* payload = esp_hap_get_setup_payload(hapString(app_config::kHomeKitSetupCode),
                                              hapString(app_config::kHomeKitSetupId),
                                              false,
                                              HAP_CID_THERMOSTAT);
    if (payload != nullptr) {
        std::strncpy(setup_payload, payload, sizeof(setup_payload) - 1);
        setup_payload[sizeof(setup_payload) - 1] = '\0';
        std::free(payload);
    }

    esp_event_handler_register(HAP_EVENT, ESP_EVENT_ANY_ID, hapEventHandler, nullptr);

    if (hap_start() != HAP_SUCCESS) {
        setLastError("hap_start failed");
        return ESP_FAIL;
    }

    started = true;
    syncFromMock();
    setLastEvent("started");
    ESP_LOGI(TAG,
             "HomeKit started: name=%s setup_code=%s setup_id=%s payload=%s paired=%d",
             app_config::kHomeKitAccessoryName,
             app_config::kHomeKitSetupCode,
             app_config::kHomeKitSetupId,
             setup_payload,
             hap_get_paired_controller_count());
    return ESP_OK;
}

Status getStatus() {
    Status status{};
    status.enabled = app_config::kHomeKitEnabled;
    status.started = started;
    status.pairedControllers = started ? hap_get_paired_controller_count() : 0;
    status.accessoryName = app_config::kHomeKitAccessoryName;
    status.setupCode = app_config::kHomeKitSetupCode;
    status.setupId = app_config::kHomeKitSetupId;
    status.setupPayload = setup_payload;
    status.lastEvent = last_event;
    status.lastError = last_error;
    return status;
}

void syncFromMock() {
    if (!started) {
        return;
    }

    const cn105_core::MockState state = cn105_core::getMockState();
    updateCharUInt8(current_state_char, currentStateFromMock(state));
    updateCharUInt8(target_state_char, targetStateFromMock(state));
    updateCharFloat(current_temp_char, fahrenheitToCelsius(state.roomTemperatureF));
    updateCharFloat(target_temp_char, fahrenheitToCelsius(state.targetTemperatureF));
    updateCharUInt8(temp_units_char, kDisplayFahrenheit);
    updateCharUInt8(fan_active_char, activeFromMock(state));
    updateCharFloat(fan_speed_char, fanSpeedFromMock(state));
    updateCharUInt8(fan_current_state_char, currentFanStateFromMock(state));
    updateCharUInt8(fan_target_state_char, targetFanStateFromMock(state));
    updateCharUInt8(fan_swing_char, swingFromMock(state));
    updateCharUInt8(vertical_slat_state_char, slatStateFromValue(state.vane));
    updateCharInt(vertical_current_tilt_char, verticalTiltFromVane(state.vane));
    updateCharInt(vertical_target_tilt_char, verticalTiltFromVane(state.vane));
    updateCharUInt8(wide_slat_state_char, slatStateFromValue(state.wideVane));
    updateCharInt(wide_current_tilt_char, horizontalTiltFromWideVane(state.wideVane));
    updateCharInt(wide_target_tilt_char, horizontalTiltFromWideVane(state.wideVane));
}

}  // namespace homekit_bridge
