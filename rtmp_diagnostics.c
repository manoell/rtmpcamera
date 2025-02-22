#include "rtmp_diagnostics.h"
#include "rtmp_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <time.h>

#define RTMP_LOG_BUFFER_SIZE 4096
#define RTMP_MAX_LOG_FILES 5
#define RTMP_MAX_FILE_SIZE (10 * 1024 * 1024)  // 10MB per file
#define RTMP_MAX_EVENTS 1000

typedef struct {
    rtmp_diagnostic_event_t events[RTMP_MAX_EVENTS];
    size_t head;
    size_t tail;
    pthread_mutex_t mutex;
} event_ring_buffer_t;

typedef struct {
    int enabled;
    FILE *log_file;
    char log_path[256];
    int log_level;
    pthread_mutex_t log_mutex;
    event_ring_buffer_t event_buffer;
    rtmp_diagnostic_callbacks_t callbacks;
    void *callback_data;
} rtmp_diagnostics_ctx_t;

static rtmp_diagnostics_ctx_t diag_ctx = {0};

// Initialize diagnostics system
int rtmp_diagnostics_init(const char *log_path, int log_level) {
    if (diag_ctx.enabled) {
        return RTMP_DIAG_ALREADY_INITIALIZED;
    }
    
    // Initialize mutexes
    if (pthread_mutex_init(&diag_ctx.log_mutex, NULL) != 0 ||
        pthread_mutex_init(&diag_ctx.event_buffer.mutex, NULL) != 0) {
        return RTMP_DIAG_MUTEX_ERROR;
    }
    
    // Set log path and level
    if (log_path) {
        strncpy(diag_ctx.log_path, log_path, sizeof(diag_ctx.log_path) - 1);
        
        // Open log file
        diag_ctx.log_file = fopen(log_path, "a");
        if (!diag_ctx.log_file) {
            pthread_mutex_destroy(&diag_ctx.log_mutex);
            pthread_mutex_destroy(&diag_ctx.event_buffer.mutex);
            return RTMP_DIAG_FILE_ERROR;
        }
    }
    
    diag_ctx.log_level = log_level;
    diag_ctx.enabled = 1;
    
    rtmp_log_info("Diagnostics system initialized");
    return RTMP_DIAG_SUCCESS;
}

// Log message with variable arguments
void rtmp_log_message(int level, const char *format, ...) {
    if (!diag_ctx.enabled || level > diag_ctx.log_level) return;
    
    char buffer[RTMP_LOG_BUFFER_SIZE];
    va_list args;
    
    // Get current time
    time_t now;
    time(&now);
    struct tm *timeinfo = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
    
    // Format message
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    pthread_mutex_lock(&diag_ctx.log_mutex);
    
    // Write to file if enabled
    if (diag_ctx.log_file) {
        fprintf(diag_ctx.log_file, "[%s] [%s] %s\n",
                timestamp,
                rtmp_diagnostics_level_string(level),
                buffer);
        fflush(diag_ctx.log_file);
        
        // Check file size and rotate if needed
        if (ftell(diag_ctx.log_file) > RTMP_MAX_FILE_SIZE) {
            rtmp_diagnostics_rotate_logs();
        }
    }
    
    // Call callback if registered
    if (diag_ctx.callbacks.log_callback) {
        diag_ctx.callbacks.log_callback(level, buffer, diag_ctx.callback_data);
    }
    
    pthread_mutex_unlock(&diag_ctx.log_mutex);
}

// Convenience logging functions
void rtmp_log_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[RTMP_LOG_BUFFER_SIZE];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    rtmp_log_message(RTMP_LOG_ERROR, buffer);
}

void rtmp_log_warning(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[RTMP_LOG_BUFFER_SIZE];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    rtmp_log_message(RTMP_LOG_WARNING, buffer);
}

void rtmp_log_info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[RTMP_LOG_BUFFER_SIZE];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    rtmp_log_message(RTMP_LOG_INFO, buffer);
}

void rtmp_log_debug(const char *format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[RTMP_LOG_BUFFER_SIZE];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    rtmp_log_message(RTMP_LOG_DEBUG, buffer);
}

// Record diagnostic event
void rtmp_diagnostics_record_event(rtmp_diagnostic_event_type_t type,
                                 const char *description,
                                 const void *data,
                                 size_t data_size) {
    if (!diag_ctx.enabled) return;
    
    pthread_mutex_lock(&diag_ctx.event_buffer.mutex);
    
    // Calculate next position
    size_t next_tail = (diag_ctx.event_buffer.tail + 1) % RTMP_MAX_EVENTS;
    
    // Check if buffer is full
    if (next_tail == diag_ctx.event_buffer.head) {
        // Remove oldest event
        diag_ctx.event_buffer.head = (diag_ctx.event_buffer.head + 1) % RTMP_MAX_EVENTS;
    }
    
    // Add new event
    rtmp_diagnostic_event_t *event = &diag_ctx.event_buffer.events[diag_ctx.event_buffer.tail];
    event->type = type;
    event->timestamp = rtmp_utils_get_time_ms();
    strncpy(event->description, description, sizeof(event->description) - 1);
    
    // Copy event data if provided
    if (data && data_size > 0) {
        size_t copy_size = data_size < sizeof(event->data) ? data_size : sizeof(event->data);
        memcpy(event->data, data, copy_size);
        event->data_size = copy_size;
    } else {
        event->data_size = 0;
    }
    
    diag_ctx.event_buffer.tail = next_tail;
    
    // Notify callback if registered
    if (diag_ctx.callbacks.event_callback) {
        diag_ctx.callbacks.event_callback(event, diag_ctx.callback_data);
    }
    
    pthread_mutex_unlock(&diag_ctx.event_buffer.mutex);
}

// Get diagnostic events
size_t rtmp_diagnostics_get_events(rtmp_diagnostic_event_t *events,
                                 size_t max_events,
                                 uint64_t since_timestamp) {
    if (!diag_ctx.enabled || !events || max_events == 0) return 0;
    
    size_t count = 0;
    pthread_mutex_lock(&diag_ctx.event_buffer.mutex);
    
    size_t current = diag_ctx.event_buffer.head;
    while (current != diag_ctx.event_buffer.tail && count < max_events) {
        rtmp_diagnostic_event_t *event = &diag_ctx.event_buffer.events[current];
        if (event->timestamp > since_timestamp) {
            memcpy(&events[count++], event, sizeof(rtmp_diagnostic_event_t));
        }
        current = (current + 1) % RTMP_MAX_EVENTS;
    }
    
    pthread_mutex_unlock(&diag_ctx.event_buffer.mutex);
    return count;
}

// Set diagnostic callbacks
void rtmp_diagnostics_set_callbacks(const rtmp_diagnostic_callbacks_t *callbacks,
                                  void *user_data) {
    if (!diag_ctx.enabled || !callbacks) return;
    
    pthread_mutex_lock(&diag_ctx.log_mutex);
    memcpy(&diag_ctx.callbacks, callbacks, sizeof(rtmp_diagnostic_callbacks_t));
    diag_ctx.callback_data = user_data;
    pthread_mutex_unlock(&diag_ctx.log_mutex);
}

// Rotate log files
static void rtmp_diagnostics_rotate_logs(void) {
    if (!diag_ctx.log_file || !diag_ctx.log_path[0]) return;
    
    // Close current log file
    fclose(diag_ctx.log_file);
    
    // Rotate existing log files
    char old_path[270], new_path[270];
    for (int i = RTMP_MAX_LOG_FILES - 1; i > 0; i--) {
        snprintf(old_path, sizeof(old_path), "%s.%d", diag_ctx.log_path, i - 1);
        snprintf(new_path, sizeof(new_path), "%s.%d", diag_ctx.log_path, i);
        rename(old_path, new_path);
    }
    
    // Rename current log file
    snprintf(new_path, sizeof(new_path), "%s.0", diag_ctx.log_path);
    rename(diag_ctx.log_path, new_path);
    
    // Open new log file
    diag_ctx.log_file = fopen(diag_ctx.log_path, "a");
}

// Get log level string
const char* rtmp_diagnostics_level_string(int level) {
    switch (level) {
        case RTMP_LOG_ERROR:   return "ERROR";
        case RTMP_LOG_WARNING: return "WARNING";
        case RTMP_LOG_INFO:    return "INFO";
        case RTMP_LOG_DEBUG:   return "DEBUG";
        default:               return "UNKNOWN";
    }
}

// Clean up diagnostics system
void rtmp_diagnostics_cleanup(void) {
    if (!diag_ctx.enabled) return;
    
    rtmp_log_info("Diagnostics system shutting down");
    
    pthread_mutex_lock(&diag_ctx.log_mutex);
    
    if (diag_ctx.log_file) {
        fclose(diag_ctx.log_file);
        diag_ctx.log_file = NULL;
    }
    
    diag_ctx.enabled = 0;
    
    pthread_mutex_unlock(&diag_ctx.log_mutex);
    
    pthread_mutex_destroy(&diag_ctx.log_mutex);
    pthread_mutex_destroy(&diag_ctx.event_buffer.mutex);
}

// Dump diagnostic information
void rtmp_diagnostics_dump_info(void) {
    if (!diag_ctx.enabled) return;
    
    rtmp_log_info("=== Diagnostic Information ===");
    rtmp_log_info("Log Level: %s", rtmp_diagnostics_level_string(diag_ctx.log_level));
    rtmp_log_info("Log Path: %s", diag_ctx.log_path);
    
    pthread_mutex_lock(&diag_ctx.event_buffer.mutex);
    
    size_t event_count = 0;
    if (diag_ctx.event_buffer.tail >= diag_ctx.event_buffer.head) {
        event_count = diag_ctx.event_buffer.tail - diag_ctx.event_buffer.head;
    } else {
        event_count = RTMP_MAX_EVENTS - diag_ctx.event_buffer.head + diag_ctx.event_buffer.tail;
    }
    
    rtmp_log_info("Events in Buffer: %zu", event_count);
    
    pthread_mutex_unlock(&diag_ctx.event_buffer.mutex);
}