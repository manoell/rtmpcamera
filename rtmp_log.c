#include "rtmp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#define LOG_FILE "/var/tmp/rtmp_debug.log"
#define MAX_LOG_SIZE (10 * 1024 * 1024) // 10MB max file size
#define MAX_LOG_LINE 1024

static FILE* log_file = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static log_level_t current_log_level = LOG_DEBUG;

static const char* level_strings[] = {
    "DEBUG",
    "INFO",
    "WARNING",
    "ERROR"
};

static void rotate_log_if_needed(void) {
    struct stat st;
    if (stat(LOG_FILE, &st) == 0) {
        if (st.st_size >= MAX_LOG_SIZE) {
            char backup[256];
            time_t now = time(NULL);
            struct tm* tm_info = localtime(&now);
            
            snprintf(backup, sizeof(backup), "%s.%04d%02d%02d-%02d%02d%02d",
                    LOG_FILE,
                    tm_info->tm_year + 1900,
                    tm_info->tm_mon + 1,
                    tm_info->tm_mday,
                    tm_info->tm_hour,
                    tm_info->tm_min,
                    tm_info->tm_sec);
            
            fclose(log_file);
            rename(LOG_FILE, backup);
            log_file = fopen(LOG_FILE, "w");
            
            if (log_file) {
                setvbuf(log_file, NULL, _IOLBF, 0);
                log_message(LOG_INFO, "Log file rotated to %s", backup);
            }
        }
    }
}

int init_logger(void) {
    pthread_mutex_lock(&log_mutex);
    
    if (log_file == NULL) {
        log_file = fopen(LOG_FILE, "a");
        if (log_file == NULL) {
            fprintf(stderr, "Failed to open log file %s: %s\n", LOG_FILE, strerror(errno));
            pthread_mutex_unlock(&log_mutex);
            return -1;
        }
        
        // Set line buffering
        setvbuf(log_file, NULL, _IOLBF, 0);
        
        // Write initialization message
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        fprintf(log_file, "\n[%04d-%02d-%02d %02d:%02d:%02d][INFO] *** RTMP Server Started ***\n",
                tm_info->tm_year + 1900,
                tm_info->tm_mon + 1,
                tm_info->tm_mday,
                tm_info->tm_hour,
                tm_info->tm_min,
                tm_info->tm_sec);
    }
    
    pthread_mutex_unlock(&log_mutex);
    return 0;
}

void close_logger(void) {
    pthread_mutex_lock(&log_mutex);
    if (log_file) {
        // Write shutdown message
        time_t now = time(NULL);
        struct tm* tm_info = localtime(&now);
        fprintf(log_file, "[%04d-%02d-%02d %02d:%02d:%02d][INFO] *** RTMP Server Shutdown ***\n",
                tm_info->tm_year + 1900,
                tm_info->tm_mon + 1,
                tm_info->tm_mday,
                tm_info->tm_hour,
                tm_info->tm_min,
                tm_info->tm_sec);
        
        fclose(log_file);
        log_file = NULL;
    }
    pthread_mutex_unlock(&log_mutex);
}

void log_message(log_level_t level, const char* format, ...) {
    if (level < current_log_level || !log_file) return;
    
    pthread_mutex_lock(&log_mutex);
    
    // Rotate log if needed
    rotate_log_if_needed();
    
    // Get current time
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    
    // Write timestamp and level
    fprintf(log_file, "[%04d-%02d-%02d %02d:%02d:%02d][%s] ",
            tm_info->tm_year + 1900,
            tm_info->tm_mon + 1,
            tm_info->tm_mday,
            tm_info->tm_hour,
            tm_info->tm_min,
            tm_info->tm_sec,
            level_strings[level]);
    
    // Write formatted message
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    
    // Add newline if not present
    if (format[strlen(format) - 1] != '\n') {
        fprintf(log_file, "\n");
    }
    
    // Flush buffer
    fflush(log_file);
    
    pthread_mutex_unlock(&log_mutex);
}