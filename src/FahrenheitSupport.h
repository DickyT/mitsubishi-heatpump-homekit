#pragma once

#include <cmath>
#include <cstddef>

enum class FahrenheitMode {
    OFF = 0,
    STANDARD = 1,
    ALT = 2
};

struct FahrenheitPoint {
    int fahrenheit;
    float celsius;
};

class FahrenheitSupport {
public:
    explicit FahrenheitSupport(FahrenheitMode mode = FahrenheitMode::STANDARD)
        : mode_(mode) {
    }

    void setMode(FahrenheitMode mode) {
        mode_ = mode;
    }

    float setpointFahrenheitToDeviceCelsius(float fahrenheit) const {
        if (std::isnan(fahrenheit)) {
            return NAN;
        }
        if (mode_ == FahrenheitMode::OFF) {
            return (fahrenheit - 32.0f) / 1.8f;
        }

        const FahrenheitPoint* table = activeTable();
        size_t size = activeTableSize();
        const FahrenheitPoint* point = nearestByFahrenheit(table, size, fahrenheit);
        if (!point) {
            return (fahrenheit - 32.0f) / 1.8f;
        }
        return point->celsius;
    }

    float deviceCelsiusToSetpointFahrenheit(float celsius) const {
        if (std::isnan(celsius)) {
            return NAN;
        }
        if (mode_ == FahrenheitMode::OFF) {
            return (celsius * 1.8f) + 32.0f;
        }

        const FahrenheitPoint* table = activeTable();
        size_t size = activeTableSize();
        const FahrenheitPoint* point = nearestByCelsius(table, size, celsius);
        if (!point) {
            return (celsius * 1.8f) + 32.0f;
        }
        return static_cast<float>(point->fahrenheit);
    }

    float deviceCelsiusToDisplayFahrenheit(float celsius) const {
        if (std::isnan(celsius)) {
            return NAN;
        }
        return deviceCelsiusToSetpointFahrenheit(celsius);
    }

private:
    FahrenheitMode mode_;

    static constexpr FahrenheitPoint standardTable_[] = {
        {61, 16.0f}, {62, 16.5f}, {63, 17.0f}, {64, 17.5f}, {65, 18.0f},
        {66, 18.5f}, {67, 19.0f}, {68, 20.0f}, {69, 21.0f}, {70, 21.5f},
        {71, 22.0f}, {72, 22.5f}, {73, 23.0f}, {74, 23.5f}, {75, 24.0f},
        {76, 24.5f}, {77, 25.0f}, {78, 25.5f}, {79, 26.0f}, {80, 26.5f},
        {81, 27.0f}, {82, 27.5f}, {83, 28.0f}, {84, 28.5f}, {85, 29.0f},
        {86, 29.5f}, {87, 30.0f}, {88, 30.5f}
    };

    static constexpr FahrenheitPoint altTable_[] = {
        {61, 16.0f}, {62, 16.5f}, {63, 17.0f}, {64, 18.0f}, {65, 18.5f},
        {66, 19.0f}, {67, 19.5f}, {68, 20.0f}, {69, 20.5f}, {70, 21.0f},
        {71, 21.5f}, {72, 22.0f}, {73, 23.0f}, {74, 23.5f}, {75, 24.0f},
        {76, 24.5f}, {77, 25.0f}, {78, 25.5f}, {79, 26.0f}, {80, 26.5f},
        {81, 27.0f}, {82, 28.0f}, {83, 28.5f}, {84, 29.0f}, {85, 29.5f},
        {86, 30.0f}, {87, 30.5f}, {88, 31.0f}
    };

    const FahrenheitPoint* activeTable() const {
        return mode_ == FahrenheitMode::ALT ? altTable_ : standardTable_;
    }

    size_t activeTableSize() const {
        return mode_ == FahrenheitMode::ALT ? (sizeof(altTable_) / sizeof(FahrenheitPoint))
                                            : (sizeof(standardTable_) / sizeof(FahrenheitPoint));
    }

    static const FahrenheitPoint* nearestByFahrenheit(const FahrenheitPoint* table, size_t size, float fahrenheit) {
        if (!table || size == 0) {
            return nullptr;
        }

        const FahrenheitPoint* best = &table[0];
        float bestDistance = fabsf(static_cast<float>(best->fahrenheit) - fahrenheit);

        for (size_t i = 1; i < size; ++i) {
            float distance = fabsf(static_cast<float>(table[i].fahrenheit) - fahrenheit);
            if (distance < bestDistance) {
                best = &table[i];
                bestDistance = distance;
            }
        }

        return best;
    }

    static const FahrenheitPoint* nearestByCelsius(const FahrenheitPoint* table, size_t size, float celsius) {
        if (!table || size == 0) {
            return nullptr;
        }

        const FahrenheitPoint* best = &table[0];
        float bestDistance = fabsf(best->celsius - celsius);

        for (size_t i = 1; i < size; ++i) {
            float distance = fabsf(table[i].celsius - celsius);
            if (distance < bestDistance) {
                best = &table[i];
                bestDistance = distance;
            }
        }

        return best;
    }
};
