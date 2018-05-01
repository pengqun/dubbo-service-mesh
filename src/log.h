#ifndef __LOG_H__
#define __LOG_H__

#include <syslog.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

void dolog(int32_t priority, const char *format, ...);

#define FATAL 0
#define ERR   1
#define INFO  2
#define DEBUG 3
#define WARN  4
#define LOG_LEVEL_MAX  5

void init_log(char *logfile_name);

#ifdef NO_LOG
#define log_msg(type, format, ...)
#else

/* If you want to debug some file (which means you print its debug
 * statement), just define macro 'DEBUG_THIS_FILE' before including
 * this header file */
#ifdef DEBUG_THIS_FILE
#define log_msg(type, format, ...) \
    do { \
        _log_msg(type, __FILE__, __LINE__, format, ##__VA_ARGS__); \
    } while (0)
#else
#define log_msg(type, format, ...) \
    do { \
        if (type != DEBUG) _log_msg(type, __FILE__, __LINE__, format, ##__VA_ARGS__); \
    } while (0)
#endif

#endif

void _log_msg(int32_t type, const char *file, int32_t line, const char *format, ...);


#ifdef DEBUG_THIS_FILE
#define log_msg_r(type, format, ...) \
    do { \
        _log_msg_r(type, __FILE__, __LINE__, format, ##__VA_ARGS__); \
    } while (0)
#else
#define log_msg_r(type, format, ...) \
    do { \
        if (type != DEBUG) _log_msg_r(type, __FILE__, __LINE__, format, ##__VA_ARGS__); \
    } while (0)
#endif

void _log_msg_r(int32_t type, const char *file, int32_t line, const char *format, ...);

#endif
