#include "rtmp_diagnostics.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <pthread.h>

// Log levels
#define LOG_LEVEL_DEBUG   0
#define LOG_LEVEL_INFO    1
#define LOG_LEVEL_WARNING 2
#define LOG_LEVEL_ERROR   3

// Maximum log message length
#define MAX_LOG_MESSAGE 1024

// Log context
static struct {
    rtmp_log_callback_t callback;
    void *callback_context;
    FILE *log_file;
    int log_level;
    bool initialized;
    pthread_mutex_t mutex;
} log_context = {
    .callback = NULL,
    .callback_context = NULL,
    .log_file = NULL,
    .log_level = LOG_LEVEL_INFO,
    .initialized = false
};

void rtmp_diagnostic_init(void) {
    if (log_context.initialized) return;
    
    pthread_mutex_init(&log_context.mutex, NULL);
    log_context.initialized = true;
}

void rtmp_diagnostic_set_callback(rtmp_log_callback_t callback, void *context) {
    pthread_mutex_lock(&log_context.mutex);
    log_context.callback = callback;
    log_context.callback_context = context;
    pthread_mutex_unlock(&log_context.mutex);
}

bool rtmp_diagnostic_set_log_file(const char *filename) {
    if (!filename) return false;
    
    pthread_mutex_lock(&log_context.mutex);
    
    if (log_context.log_file) {
        fclose(log_context.log_file);
        log_context.log_file = NULL;
    }
    
    log_context.log_file = fopen(filename, "a");
    bool success = (log_context.log_file != NULL);
    
    pthread_mutex_unlock(&log_context.mutex);
    
    return success;
}

void rtmp_diagnostic_set_level(int level) {
    if (level < LOG_LEVEL_DEBUG || level > LOG_LEVEL_ERROR) return;
    
    pthread_mutex_lock(&log_context.mutex);
    log_context.log_level = level;
    pthread_mutex_unlock(&log_context.mutex);
}

void rtmp_diagnostic_log(const char *format, ...) {
    if (!format || !log_context.initialized) return;
    
    char message[MAX_LOG_MESSAGE];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    pthread_mutex_lock(&log_context.mutex);
    
    // Get current time
    time_t now;
    time(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    // Log to file if enabled
    if (log_context.log_file) {
        fprintf(log_context.log_file, "[%s] %s\n", timestamp, message);
        fflush(log_context.log_file);
    }
    
    // Call callback if set
    if (log_context.callback) {
        RTMPLogMessage log_message = {
            .timestamp = timestamp,
            .message = message,
            .level = log_context.log_level
        };
        log_context.callback(&log_message, log_context.callback_context);
    }
    
    pthread_mutex_unlock(&log_context.mutex);
}

void rtmp_diagnostic_debug(const char *format, ...) {
    if (log_context.log_level > LOG_LEVEL_DEBUG) return;
    
    char message[MAX_LOG_MESSAGE];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    rtmp_diagnostic_log("[DEBUG] %s", message);
}

void rtmp_diagnostic_info(const char *format, ...) {
    if (log_context.log_level > LOG_LEVEL_INFO) return;
    
    char message[MAX_LOG_MESSAGE];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    rtmp_diagnostic_log("[INFO] %s", message);
}

void rtmp_diagnostic_warning(const char *format, ...) {
    if (log_context.log_level > LOG_LEVEL_WARNING) return;
    
    char message[MAX_LOG_MESSAGE];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    rtmp_diagnostic_log("[WARNING] %s", message);
}

void rtmp_diagnostic_error(const char *format, ...) {
    if (log_context.log_level > LOG_LEVEL_ERROR) return;
    
    char message[MAX_LOG_MESSAGE];
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    rtmp_diagnostic_log("[ERROR] %s", message);
}

void rtmp_diagnostic_cleanup(void) {
    if (!log_context.initialized) return;
    
    pthread_mutex_lock(&log_context.mutex);
    
    if (log_context.log_file) {
        fclose(log_context.log_file);
        log_context.log_file = NULL;
    }
    
    log_context.callback = NULL;
    log_context.callback_context = NULL;
    
    pthread_mutex_unlock(&log_context.mutex);
    pthread_mutex_destroy(&log_context.mutex);
    
    log_context.initialized = false;
}