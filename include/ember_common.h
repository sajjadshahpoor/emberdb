/*
 * ember_common.h - shared macros, logging, and error codes used across
 * every EmberDB module.
 */
#ifndef EMBER_COMMON_H
#define EMBER_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#define EMBER_VERSION "0.1.0"

typedef enum {
    EMBER_OK = 0,
    EMBER_ERR = -1,
    EMBER_ERR_OOM = -2,
    EMBER_ERR_NOTFOUND = -3,
    EMBER_ERR_INVALID = -4,
    EMBER_ERR_IO = -5,
    EMBER_ERR_TYPE = -6,
    EMBER_ERR_PROTOCOL = -7,
} ember_status;

typedef enum {
    EMBER_LOG_DEBUG = 0,
    EMBER_LOG_INFO,
    EMBER_LOG_WARN,
    EMBER_LOG_ERROR,
} ember_log_level;

void ember_log_set_level(ember_log_level level);
void ember_log(ember_log_level level, const char *fmt, ...);

#define log_debug(...) ember_log(EMBER_LOG_DEBUG, __VA_ARGS__)
#define log_info(...)  ember_log(EMBER_LOG_INFO, __VA_ARGS__)
#define log_warn(...)  ember_log(EMBER_LOG_WARN, __VA_ARGS__)
#define log_error(...) ember_log(EMBER_LOG_ERROR, __VA_ARGS__)

#define EMBER_UNUSED(x) ((void)(x))

#define EMBER_MIN(a, b) ((a) < (b) ? (a) : (b))
#define EMBER_MAX(a, b) ((a) > (b) ? (a) : (b))

/* Fatal assertion that stays compiled in release builds (unlike assert()). */
#define EMBER_ASSERT(cond, msg)                                              \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "[FATAL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
            abort();                                                         \
        }                                                                    \
    } while (0)

#endif /* EMBER_COMMON_H */
