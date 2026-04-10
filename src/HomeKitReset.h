#pragma once

#include <Arduino.h>
#include <esp_err.h>
#include <nvs.h>

#include "DebugLog.h"

namespace HomeKitReset {

static bool eraseNamespace(const char* name, String& message) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        message += String(name) + ": open failed (" + esp_err_to_name(err) + ")\n";
        return false;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        message += String(name) + ": erase failed (" + esp_err_to_name(err) + ")\n";
        return false;
    }

    message += String(name) + ": erased\n";
    return true;
}

static bool eraseNamespaces(const char* const* names, size_t count, String& message) {
    bool ok = true;
    for (size_t i = 0; i < count; i++) {
        ok = eraseNamespace(names[i], message) && ok;
    }
    return ok;
}

static bool clearHomeKitData(String& message) {
    static const char* const namespaces[] = {"HAP", "SRP", "CHAR"};
    message = "HomeKit/HomeSpan pairing data cleared. Reboot when you are ready.\n";
    return eraseNamespaces(namespaces, sizeof(namespaces) / sizeof(namespaces[0]), message);
}

static bool clearAllHomeSpanData(String& message) {
    static const char* const namespaces[] = {"HAP", "SRP", "CHAR", "WIFI", "OTA", "POINT"};
    message = "All known HomeSpan NVS namespaces cleared. Reboot when you are ready.\n";
    return eraseNamespaces(namespaces, sizeof(namespaces) / sizeof(namespaces[0]), message);
}

static void rebootNow() {
    DebugLog::println("[HomeKitReset] Reboot requested from WebUI");
    delay(250);
    ESP.restart();
}

}  // namespace HomeKitReset
