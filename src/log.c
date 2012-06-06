#include <stdio.h>
#include <stdarg.h>
#include "log.h"

void _ed2kd_log( enum log_severity svrt, const char * fmt, ... )
{
    va_list ap;
    va_start(ap, fmt);

    const char *severity_str;
    switch ( svrt ) {
#ifdef DEBUG
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

    char buf[1024];
    snprintf(buf, sizeof(buf), "[%s] %s\n", severity_str, fmt);
    vfprintf(stderr, buf, ap);

    va_end(ap);
}
