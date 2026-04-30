// Kiri Bridge — shared OTA HTTP handlers.
//
// Both the production firmware (components/web) and the installer firmware
// (debug_apps/cn105_probe) handle .kiri uploads the same way: stream the
// app.bin body into the next OTA partition, verify mbedtls SHA-256 against
// an X-Kiri-Sha256 header, then on /api/ota/apply set the new boot
// partition and reboot. Only the set of allowed manifest variants differs.

#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#include <cstddef>

namespace ota_handler {

struct AcceptedVariants {
    const char* const* values;
    size_t count;
};

// Register /api/ota/info, /api/ota/upload, /api/ota/apply on the given
// server. The accepted variants list is captured by reference; callers
// must keep it alive for the lifetime of the server (a static constexpr
// array is the typical shape).
esp_err_t registerHandlers(httpd_handle_t server, AcceptedVariants accepted_variants);

}  // namespace ota_handler
