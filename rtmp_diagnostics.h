#ifndef RTMP_DIAGNOSTICS_H
#define RTMP_DIAGNOSTICS_H

#include <stdint.h>
#include <stdbool.h>

// Log message structure
typedef struct {
    const char *timestamp;
    const char *message;
    int level;
} RTMPLogMessage;

// Log callback function type
typedef void (*rtmp_log_callback_t)(const RTMPLogMessage *message, void *context);

// Initialize diagnostic system
void rtmp_diagnostic_init(void);

// Set log callback
void rtmp_diagnostic_set_callback(rtmp_log_callback_t callback, void *context);

// Set log file
bool rtmp_diagnostic_set_log_file(const char *filename);

// Set log level
void rtmp_diagnostic_set_level(int level);

// Log functions
void rtmp_diagnostic_log(const char *format, ...);
void rtmp_diagnostic_debug(const char *format, ...);
void rtmp_diagnostic_info(const char *format, ...);
void rtmp_diagnostic_warning(const char *format, ...);
void rtmp_diagnostic_error(const char *format, ...);

// Cleanup diagnostic system
void rtmp_diagnostic_cleanup(void);

#endif // RTMP_DIAGNOSTICS_H