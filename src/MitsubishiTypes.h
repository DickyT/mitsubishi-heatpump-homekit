#pragma once

#include <cmath>

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

    void reset() {
        roomTemperature = NAN;
        outsideAirTemperature = NAN;
        operating = false;
        compressorFrequency = 0.0f;
        inputPower = 0.0f;
        kWh = 0.0f;
        runtimeHours = 0.0f;
    }
};
