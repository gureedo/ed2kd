#include "log.h"
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>

#include <event2/util.h>

void _ed2kd_log(enum log_severity svrt, const char *fmt, ...)
{
    char buf[1024];
    const char *severity_str;
    va_list ap;
    va_start(ap, fmt);

    // todo: add loggin mutex

    switch (svrt) {
#ifdef USE_DEBUG
        case LOG_DBG:
                severity_str = "dbg";
                break;
#endif
        case LOG_NFO:
            severity_str = "nfo";
            break;
        case LOG_WRN:
            severity_str = "wrn";
            break;
        case LOG_ERR:
            severity_str = "err";
            break;
        default:
            severity_str = "???";
            break;
    }

    evutil_snprintf(buf, sizeof(buf), "[%s] %s\n", severity_str, fmt);
    vfprintf(stderr, buf, ap);

    va_end(ap);
}
