#ifndef RTMP_LOG_H
#define RTMP_LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// Log levels
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR
} log_level_t;

// Initialize logging system
int init_logger(void);

// Close logging system
void close_logger(void);

// Log message with specified level
void log_message(log_level_t level, const char* format, ...);

// Helper macros for easier logging
#define LOG_DEBUG(...) log_message(LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(...) log_message(LOG_INFO, __VA_ARGS__)
#define LOG_WARNING(...) log_message(LOG_WARNING, __VA_ARGS__)
#define LOG_ERROR(...) log_message(LOG_ERROR, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // RTMP_LOG_H