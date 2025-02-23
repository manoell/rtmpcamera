#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include "rtmp_diagnostics.h"
#include "rtmp_utils.h"

#define MAX_LOG_LENGTH 1024
#define MAX_TIMING_OPERATIONS 100
#define TIMING_HISTORY_SIZE 50

typedef struct {
    char operation[64];
    uint64_t start_time;
    uint64_t duration;
} timing_record_t;

typedef struct {
    rtmp_diagnostic_stats_t stats;
    rtmp_log_callback_t log_callback;
    void *callback_data;
    int min_log_level;
    uint32_t diag_flags;
    timing_record_t timing_history[TIMING_HISTORY_SIZE];
    int timing_history_index;
    pthread_mutex_t mutex;
    uint64_t timing_ids[MAX_TIMING_OPERATIONS];
    char *timing_operations[MAX_TIMING_OPERATIONS];
    int timing_count;
} rtmp_diagnostic_context_t;

static rtmp_diagnostic_context_t *diag_ctx = NULL;

static const char *log_level_strings[] = {
    "DEBUG",
    "INFO",
    "WARNING",
    "ERROR",
    "FATAL"
};

static uint64_t get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

int rtmp_diagnostic_init(void) {
    if (diag_ctx) return 0;
    
    diag_ctx = (rtmp_diagnostic_context_t*)calloc(1, sizeof(rtmp_diagnostic_context_t));
    if (!diag_ctx) return 0;
    
    pthread_mutex_init(&diag_ctx->mutex, NULL);
    diag_ctx->min_log_level = RTMP_LOG_INFO;
    diag_ctx->diag_flags = RTMP_DIAG_ALL;
    diag_ctx->stats.start_time = get_timestamp_ms();
    
    return 1;
}

void rtmp_diagnostic_shutdown(void) {
    if (!diag_ctx) return;
    
    pthread_mutex_lock(&diag_ctx->mutex);
    
    // Limpa operações de timing pendentes
    for (int i = 0; i < diag_ctx->timing_count; i++) {
        free(diag_ctx->timing_operations[i]);
    }
    
    pthread_mutex_unlock(&diag_ctx->mutex);
    pthread_mutex_destroy(&diag_ctx->mutex);
    
    free(diag_ctx);
    diag_ctx = NULL;
}

void rtmp_diagnostic_set_callback(rtmp_log_callback_t callback, void *user_data) {
    if (!diag_ctx) return;
    
    pthread_mutex_lock(&diag_ctx->mutex);
    diag_ctx->log_callback = callback;
    diag_ctx->callback_data = user_data;
    pthread_mutex_unlock(&diag_ctx->mutex);
}

void rtmp_diagnostic_set_level(int level) {
    if (!diag_ctx) return;
    diag_ctx->min_log_level = level;
}

void rtmp_diagnostic_set_flags(uint32_t flags) {
    if (!diag_ctx) return;
    diag_ctx->diag_flags = flags;
}

void rtmp_diagnostic_log(int level, const char *module, const char *format, va_list args) {
    if (!diag_ctx || level < diag_ctx->min_log_level) return;
    
    char message[MAX_LOG_LENGTH];
    char timestamp[32];
    time_t now;
    struct tm *tm_info;
    
    time(&now);
    tm_info = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    vsnprintf(message, sizeof(message), format, args);
    
    pthread_mutex_lock(&diag_ctx->mutex);
    
    if (diag_ctx->log_callback) {
        diag_ctx->log_callback(level, module, message, diag_ctx->callback_data);
    } else {
        fprintf(stderr, "[%s] %s - %s: %s\n", 
                timestamp, 
                log_level_strings[level], 
                module, 
                message);
    }
    
    if (level >= RTMP_LOG_ERROR) {
        diag_ctx->stats.error_count++;
    }
    
    pthread_mutex_unlock(&diag_ctx->mutex);
}

void rtmp_log_debug(const char *format, ...) {
    va_list args;
    va_start(args, format);
    rtmp_diagnostic_log(RTMP_LOG_DEBUG, "RTMP", format, args);
    va_end(args);
}

void rtmp_log_info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    rtmp_diagnostic_log(RTMP_LOG_INFO, "RTMP", format, args);
    va_end(args);
}

void rtmp_log_warning(const char *format, ...) {
    va_list args;
    va_start(args, format);
    rtmp_diagnostic_log(RTMP_LOG_WARNING, "RTMP", format, args);
    va_end(args);
}

void rtmp_log_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    rtmp_diagnostic_log(RTMP_LOG_ERROR, "RTMP", format, args);
    va_end(args);
}

void rtmp_log_fatal(const char *format, ...) {
    va_list args;
    va_start(args, format);
    rtmp_diagnostic_log(RTMP_LOG_FATAL, "RTMP", format, args);
    va_end(args);
}

int rtmp_diagnostic_get_stats(rtmp_diagnostic_stats_t *stats) {
    if (!diag_ctx || !stats) return 0;
    
    pthread_mutex_lock(&diag_ctx->mutex);
    
    memcpy(stats, &diag_ctx->stats, sizeof(rtmp_diagnostic_stats_t));
    stats->uptime = get_timestamp_ms() - diag_ctx->stats.start_time;
    
    pthread_mutex_unlock(&diag_ctx->mutex);
    return 1;
}

void rtmp_diagnostic_reset_stats(void) {
    if (!diag_ctx) return;
    
    pthread_mutex_lock(&diag_ctx->mutex);
    memset(&diag_ctx->stats, 0, sizeof(rtmp_diagnostic_stats_t));
    diag_ctx->stats.start_time = get_timestamp_ms();
    pthread_mutex_unlock(&diag_ctx->mutex);
}

void rtmp_diagnostic_mark_event(const char *event_name) {
    if (!diag_ctx || !event_name) return;
    
    rtmp_log_info("Event: %s", event_name);
}

uint64_t rtmp_diagnostic_start_timing(const char *operation) {
    if (!diag_ctx || !operation || diag_ctx->timing_count >= MAX_TIMING_OPERATIONS) {
        return 0;
    }
    
    pthread_mutex_lock(&diag_ctx->mutex);
    
    uint64_t timing_id = get_timestamp_ms();
    int idx = diag_ctx->timing_count++;
    
    diag_ctx->timing_ids[idx] = timing_id;
    diag_ctx->timing_operations[idx] = strdup(operation);
    
    pthread_mutex_unlock(&diag_ctx->mutex);
    
    return timing_id;
}

void rtmp_diagnostic_end_timing(uint64_t timing_id) {
    if (!diag_ctx || !timing_id) return;
    
    pthread_mutex_lock(&diag_ctx->mutex);
    
    uint64_t end_time = get_timestamp_ms();
    
    for (int i = 0; i < diag_ctx->timing_count; i++) {
        if (diag_ctx->timing_ids[i] == timing_id) {
            uint64_t duration = end_time - timing_id;
            
            // Registra no histórico
            int hist_idx = diag_ctx->timing_history_index;
            strncpy(diag_ctx->timing_history[hist_idx].operation,
                   diag_ctx->timing_operations[i],
                   sizeof(diag_ctx->timing_history[hist_idx].operation)-1);
            diag_ctx->timing_history[hist_idx].start_time = timing_id;
            diag_ctx->timing_history[hist_idx].duration = duration;
            
            diag_ctx->timing_history_index = (hist_idx + 1) % TIMING_HISTORY_SIZE;
            
            // Log se duração for significativa (> 100ms)
            if (duration > 100) {
                rtmp_log_warning("Operation '%s' took %llu ms",
                               diag_ctx->timing_operations[i],
                               duration);
            }
            
            free(diag_ctx->timing_operations[i]);
            
            // Remove da lista de operações ativas
            for (int j = i; j < diag_ctx->timing_count - 1; j++) {
                diag_ctx->timing_ids[j] = diag_ctx->timing_ids[j+1];
                diag_ctx->timing_operations[j] = diag_ctx->timing_operations[j+1];
            }
            diag_ctx->timing_count--;
            break;
        }
    }
    
    pthread_mutex_unlock(&diag_ctx->mutex);
}