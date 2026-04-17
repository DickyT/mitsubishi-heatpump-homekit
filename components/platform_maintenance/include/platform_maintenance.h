#pragma once

#include <cstddef>

namespace platform_maintenance {

struct Result {
    bool ok = false;
    bool rebooting = false;
    char action[40] = "";
    char message[512] = "";
};

Result resetHomeKit();
Result clearLogs();
Result clearSpiffs();
Result clearAllNvs();
void rebootSoon();

}  // namespace platform_maintenance
