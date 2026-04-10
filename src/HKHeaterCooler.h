#pragma once

#include "HomeSpan.h"

#include "MitsubishiProtocol.h"

static float fanToPercent(const char* fan) {
    if (!fan) return 0;
    if (strcmp(fan, "QUIET") == 0) return 14;
    if (strcmp(fan, "1") == 0) return 28;
    if (strcmp(fan, "2") == 0) return 42;
    if (strcmp(fan, "3") == 0) return 71;
    if (strcmp(fan, "4") == 0) return 100;
    return 0;
}

static const char* percentToFan(float pct) {
    if (pct <= 0) return "AUTO";
    if (pct <= 20) return "QUIET";
    if (pct <= 35) return "1";
    if (pct <= 55) return "2";
    if (pct <= 80) return "3";
    return "4";
}

class HKHeaterCooler : public Service::HeaterCooler {
    MitsubishiProtocol* proto_;

    SpanCharacteristic* active_;
    SpanCharacteristic* currentTemp_;
    SpanCharacteristic* currentState_;
    SpanCharacteristic* targetState_;
    SpanCharacteristic* coolingThreshold_;
    SpanCharacteristic* heatingThreshold_;
    SpanCharacteristic* rotationSpeed_;
    SpanCharacteristic* swingMode_;
    SpanCharacteristic* displayUnits_;

    uint32_t lastSyncMs_ = 0;

public:
    explicit HKHeaterCooler(MitsubishiProtocol* proto)
        : Service::HeaterCooler(), proto_(proto) {
        active_ = new Characteristic::Active(0, true);
        currentTemp_ = new Characteristic::CurrentTemperature(20);
        currentState_ = new Characteristic::CurrentHeaterCoolerState(0);
        targetState_ = new Characteristic::TargetHeaterCoolerState(0, true);
        coolingThreshold_ = new Characteristic::CoolingThresholdTemperature(26, true);
        heatingThreshold_ = new Characteristic::HeatingThresholdTemperature(20, true);
        rotationSpeed_ = new Characteristic::RotationSpeed(0, true);
        swingMode_ = new Characteristic::SwingMode(0, true);
        displayUnits_ = new Characteristic::TemperatureDisplayUnits(1, true);

        coolingThreshold_->setRange(16, 31, 0.5);
        heatingThreshold_->setRange(16, 31, 0.5);
        targetState_->setValidValues(3, 0, 1, 2);
    }

    boolean update() override {
        const heatpumpSettings& current = proto_->getCurrentSettings();

        String power = current.power ? current.power : "OFF";
        String mode = current.mode ? current.mode : "AUTO";
        String fan = current.fan ? current.fan : "AUTO";
        String vane = current.vane ? current.vane : "AUTO";
        String wideVane = current.wideVane ? current.wideVane : "AIRFLOW CONTROL";
        float targetTemperatureF = proto_->getTargetTemperatureF();

        if (active_->updated()) {
            power = active_->getNewVal() ? "ON" : "OFF";
        }

        if (targetState_->updated()) {
            power = "ON";
            switch (targetState_->getNewVal()) {
                case 1:
                    mode = "HEAT";
                    break;
                case 2:
                    mode = "COOL";
                    break;
                default:
                    mode = "AUTO";
                    break;
            }
        }

        if (coolingThreshold_->updated()) {
            targetTemperatureF = celsiusToFahrenheit(coolingThreshold_->getNewVal<float>());
        }

        if (heatingThreshold_->updated()) {
            targetTemperatureF = celsiusToFahrenheit(heatingThreshold_->getNewVal<float>());
        }

        if (rotationSpeed_->updated()) {
            fan = percentToFan(rotationSpeed_->getNewVal<float>());
        }

        if (swingMode_->updated()) {
            vane = swingMode_->getNewVal() ? "SWING" : "AUTO";
        }

        proto_->applyMockRemoteSettings(power, mode, targetTemperatureF, fan, vane, wideVane);
        syncFromProtocol();
        return true;
    }

    void loop() override {
        if (millis() - lastSyncMs_ < 1000) {
            return;
        }
        lastSyncMs_ = millis();
        syncFromProtocol();
    }

private:
    static float celsiusToFahrenheit(float celsius) {
        return (celsius * 1.8f) + 32.0f;
    }

    int calcCurrentState(const heatpumpSettings& settings, const heatpumpStatus& status) {
        if (!settings.power || strcmp(settings.power, "OFF") == 0) return 0;
        if (!status.operating) return 1;

        if (settings.mode && strcmp(settings.mode, "HEAT") == 0) return 2;
        if (settings.mode && (strcmp(settings.mode, "COOL") == 0 || strcmp(settings.mode, "DRY") == 0)) return 3;
        if (settings.mode && strcmp(settings.mode, "AUTO") == 0) {
            if (!isnan(status.roomTemperature) && settings.temperature > 0) {
                return status.roomTemperature < settings.temperature ? 2 : 3;
            }
        }
        return 1;
    }

    void syncFromProtocol() {
        const heatpumpSettings& s = proto_->getCurrentSettings();
        const heatpumpStatus& st = proto_->getCurrentStatus();

        int power = (s.power && strcmp(s.power, "ON") == 0) ? 1 : 0;
        if (power != active_->getVal()) {
            active_->setVal(power);
        }

        int hkTarget = 0;
        if (s.mode && strcmp(s.mode, "HEAT") == 0) hkTarget = 1;
        else if (s.mode && strcmp(s.mode, "COOL") == 0) hkTarget = 2;
        if (hkTarget != targetState_->getVal()) {
            targetState_->setVal(hkTarget);
        }

        if (s.temperature > 0) {
            float celsius = s.temperature;
            if (fabsf(celsius - coolingThreshold_->getVal<float>()) > 0.01f) {
                coolingThreshold_->setVal(celsius);
            }
            if (fabsf(celsius - heatingThreshold_->getVal<float>()) > 0.01f) {
                heatingThreshold_->setVal(celsius);
            }
        }

        float hkFan = fanToPercent(s.fan);
        if (fabsf(hkFan - rotationSpeed_->getVal<float>()) > 0.01f) {
            rotationSpeed_->setVal(hkFan);
        }

        int swing = (s.vane && strcmp(s.vane, "SWING") == 0) ? 1 : 0;
        if (swing != swingMode_->getVal()) {
            swingMode_->setVal(swing);
        }

        if (displayUnits_->getVal() != 1) {
            displayUnits_->setVal(1);
        }

        if (!isnan(st.roomTemperature) && fabsf(st.roomTemperature - currentTemp_->getVal<float>()) > 0.01f) {
            currentTemp_->setVal(st.roomTemperature);
        }

        int currentState = calcCurrentState(s, st);
        if (currentState != currentState_->getVal()) {
            currentState_->setVal(currentState);
        }
    }
};
