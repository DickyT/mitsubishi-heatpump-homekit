#pragma once

#include "esp_err.h"

namespace platform_fs {

esp_err_t init();
const char* basePath();
void logStats();

}  // namespace platform_fs
