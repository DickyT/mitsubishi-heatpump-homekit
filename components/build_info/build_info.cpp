#include "build_info.h"

#include <cstdio>
#include <cstring>

namespace {

int monthNumber(const char* month) {
    if (month == nullptr) {
        return 0;
    }
    if (std::strcmp(month, "Jan") == 0) return 1;
    if (std::strcmp(month, "Feb") == 0) return 2;
    if (std::strcmp(month, "Mar") == 0) return 3;
    if (std::strcmp(month, "Apr") == 0) return 4;
    if (std::strcmp(month, "May") == 0) return 5;
    if (std::strcmp(month, "Jun") == 0) return 6;
    if (std::strcmp(month, "Jul") == 0) return 7;
    if (std::strcmp(month, "Aug") == 0) return 8;
    if (std::strcmp(month, "Sep") == 0) return 9;
    if (std::strcmp(month, "Oct") == 0) return 10;
    if (std::strcmp(month, "Nov") == 0) return 11;
    if (std::strcmp(month, "Dec") == 0) return 12;
    return 0;
}

}  // namespace

namespace build_info {

const char* firmwareVersion() {
    static char version[24] = "";
    if (version[0] != '\0') {
        return version;
    }

    char month[4] = {};
    int day = 0;
    int year = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    std::sscanf(__DATE__, "%3s %d %d", month, &day, &year);
    std::sscanf(__TIME__, "%d:%d:%d", &hour, &minute, &second);
    const int month_num = monthNumber(month);

    std::snprintf(version,
                  sizeof(version),
                  "%04d.%02d%02d.%02d%02d%02d",
                  year,
                  month_num,
                  day,
                  hour,
                  minute,
                  second);
    return version;
}

}  // namespace build_info
