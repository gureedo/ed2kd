#ifndef ED2KD_LOG_H
#define ED2KD_LOG_H

enum log_severity {
#ifdef USE_DEBUG
        LOG_DBG,
#endif
    LOG_NFO,
    LOG_WRN,
    LOG_ERR
};

void _ed2kd_log(enum log_severity svrt, const char *fmt, ...);

#define ED2KD_LOGNFO(msg, args...) _ed2kd_log(LOG_NFO, msg, ##args)
#define ED2KD_LOGWRN(msg, args...) _ed2kd_log(LOG_WRN, msg, ##args)
#ifdef USE_DEBUG
#define ED2KD_LOGDBG(msg, args...) _ed2kd_log(LOG_DBG, msg, ##args)
#else
#define ED2KD_LOGDBG(msg, args...)
#endif
#define ED2KD_LOGERR(msg, args...) _ed2kd_log(LOG_ERR, "(%s:%d): " msg, __FILE__, __LINE__, ##args)

#endif // ED2KD_LOG_H
