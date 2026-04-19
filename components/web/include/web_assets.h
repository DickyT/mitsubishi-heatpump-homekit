#pragma once

#include <cstddef>
#include <cstdint>

namespace web_assets {

struct GzipAsset {
    const char* path;
    const uint8_t* start;
    const uint8_t* end;
    const char* contentType;
};

const GzipAsset* find(const char* path);
const char* version();

}  // namespace web_assets
