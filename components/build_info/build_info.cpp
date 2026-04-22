#include "build_info.h"

#if __has_include("build_info_generated.h")
#include "build_info_generated.h"
#else
#define BUILD_INFO_FIRMWARE_VERSION "dev"
#endif

namespace build_info {

const char* firmwareVersion() {
    return BUILD_INFO_FIRMWARE_VERSION;
}

}  // namespace build_info
