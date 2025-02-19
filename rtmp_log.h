#ifndef RTMP_LOG_H
#define RTMP_LOG_H

// Níveis de log
typedef enum {
    RTMP_LOG_DEBUG = 0,
    RTMP_LOG_INFO,
    RTMP_LOG_WARN,
    RTMP_LOG_ERROR
} rtmp_log_level_t;

// Funções de log
void log_rtmp(const char* format, ...);
void log_rtmp_level(rtmp_log_level_t level, const char* format, ...);
void rtmp_log_init(const char* log_file);
void rtmp_log_cleanup(void);

#endif // RTMP_LOG_H