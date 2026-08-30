#ifndef REIST_TLS_COMPAT_TIME_H
#define REIST_TLS_COMPAT_TIME_H

#include <stdint.h>

typedef int64_t time_t;
struct tm {
    int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year;
    int tm_wday, tm_yday, tm_isdst;
};

#endif
