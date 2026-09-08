#include "ember_common.h"
#include <stdarg.h>
#include <string.h>

static ember_log_level g_log_level = EMBER_LOG_INFO;

void ember_log_set_level(ember_log_level level) {
    g_log_level = level;
}

static const char *level_name(ember_log_level level) {
    switch (level) {
        case EMBER_LOG_DEBUG: return "DEBUG";
        case EMBER_LOG_INFO:  return "INFO";
        case EMBER_LOG_WARN:  return "WARN";
        case EMBER_LOG_ERROR: return "ERROR";
        default: return "?";
    }
}

void ember_log(ember_log_level level, const char *fmt, ...) {
    if (level < g_log_level) return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tm_info;
    localtime_r(&ts.tv_sec, &tm_info);

    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_info);

    FILE *out = (level >= EMBER_LOG_WARN) ? stderr : stdout;
    fprintf(out, "%s.%03ld [%-5s] ", timebuf, ts.tv_nsec / 1000000, level_name(level));

    va_list args;
    va_start(args, fmt);
    vfprintf(out, fmt, args);
    va_end(args);

    fputc('\n', out);
}
