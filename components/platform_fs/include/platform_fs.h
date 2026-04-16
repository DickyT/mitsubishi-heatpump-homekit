#pragma once

#include "esp_err.h"

#include <cstddef>

namespace platform_fs {

struct Status {
    bool mounted = false;
    size_t totalBytes = 0;
    size_t usedBytes = 0;
    size_t freeBytes = 0;
};

esp_err_t init();
const char* basePath();
Status getStatus();
void logStats();

}  // namespace platform_fs
