#ifndef RTMP_DIAGNOSTICS_H
#define RTMP_DIAGNOSTICS_H

#include <stdint.h>
#include <stddef.h>

// Log levels
#define RTMP_LOG_ERROR 0
#define RTMP_LOG_WARNING 1
#define RTMP_LOG_INFO 2
#define RTMP_LOG_DEBUG 3

// Return codes
#define RTMP_DIAG_SUCCESS 0
#define RTMP_DIAG_ALREADY_INITIALIZED -1
#define RTMP_DIAG_MUTEX_ERROR -2
#define RTMP_DIAG_FILE_ERROR -3

// Event types
typedef enum {
    RTMP_EVENT_CONNECTION = 0,
    RTMP_EVENT_STREAM,
    RTMP_EVENT_QUALITY,
    RTMP_EVENT_ERROR,
    RTMP_EVENT_PERFORMANCE,
    RTMP_EVENT_NETWORK,
    RTMP_EVENT_CAMERA
} rtmp_diagnostic_event_type_t;

// Diagnostic event structure
typedef struct {
    rtmp_diagnostic_event_type_t type;
    uint64_t timestamp;
    char description[256];
    uint8_t data[1024];
    size_t data_size;
} rtmp_diagnostic_event_t;

// Callback functions
typedef struct {
    void (*log_callback)(int level, const char *message, void *user_data);
    void (*event_callback)(const rtmp_diagnostic_event_t *event, void *user_data);
} rtmp_diagnostic_callbacks_t;

// Core functions
int rtmp_diagnostics_init(const char *log_path, int log_level);
void rtmp_diagnostics_cleanup(void);

// Logging functions
void rtmp_log_message(int level, const char *format, ...);
void rtmp_log_error(const char *format, ...);
void rtmp_log_warning(const char *format, ...);
void rtmp_log_info(const char *format, ...);
void rtmp_log_debug(const char *format, ...);

// Event management
void rtmp_diagnostics_record_event(rtmp_diagnostic_event_type_t type,
                                 const char *description,
                                 const void *data,
                                 size_t data_size);

size_t rtmp_diagnostics_get_events(rtmp_diagnostic_event_t *events,
                                 size_t max_events,
                                 uint64_t since_timestamp);

// Callback management
void rtmp_diagnostics_set_callbacks(const rtmp_diagnostic_callbacks_t *callbacks,
                                  void *user_data);

// Utility functions
const char* rtmp_diagnostics_level_string(int level);
void rtmp_diagnostics_dump_info(void);

// Performance monitoring
typedef struct {
    uint64_t cpu_usage;       // CPU usage percentage * 100
    uint64_t memory_usage;    // Memory usage in bytes
    uint64_t network_in;      // Network input bytes/sec
    uint64_t network_out;     // Network output bytes/sec
    uint64_t frame_rate;      // Current frame rate * 100
    uint64_t dropped_frames;  // Number of dropped frames
    uint64_t latency;        // Current latency in milliseconds
} rtmp_diagnostic_performance_t;

void rtmp_diagnostics_record_performance(const rtmp_diagnostic_performance_t *perf);

// Error tracking
typedef struct {
    int code;                 // Error code
    char message[256];        // Error message
    char location[128];       // Error location (file:line)
    uint64_t timestamp;       // Error timestamp
} rtmp_diagnostic_error_t;

void rtmp_diagnostics_record_error(const rtmp_diagnostic_error_t *error);

// Status checking
typedef enum {
    RTMP_DIAG_STATUS_OK = 0,
    RTMP_DIAG_STATUS_WARNING = 1,
    RTMP_DIAG_STATUS_ERROR = 2,
    RTMP_DIAG_STATUS_CRITICAL = 3
} rtmp_diagnostic_status_t;

rtmp_diagnostic_status_t rtmp_diagnostics_check_status(void);

// Memory tracking
typedef struct {
    size_t total_allocated;   // Total bytes allocated
    size_t peak_allocated;    // Peak bytes allocated
    size_t current_allocated; // Currently allocated bytes
    size_t allocation_count;  // Number of allocations
    size_t free_count;       // Number of frees
} rtmp_diagnostic_memory_t;

void rtmp_diagnostics_track_memory(const rtmp_diagnostic_memory_t *memory);

// Network monitoring
typedef struct {
    uint64_t bytes_sent;      // Total bytes sent
    uint64_t bytes_received;  // Total bytes received
    uint64_t packets_sent;    // Total packets sent
    uint64_t packets_received;// Total packets received
    uint64_t errors;         // Network errors count
    float packet_loss;       // Packet loss percentage
    float rtt;              // Round trip time in ms
} rtmp_diagnostic_network_t;

void rtmp_diagnostics_track_network(const rtmp_diagnostic_network_t *network);

// Camera diagnostics
typedef struct {
    uint32_t width;          // Frame width
    uint32_t height;         // Frame height
    float fps;              // Current FPS
    float exposure;         // Current exposure
    float iso;             // Current ISO
    float focus;           // Current focus value
    uint32_t frames_captured; // Total frames captured
    uint32_t frames_dropped;  // Total frames dropped
} rtmp_diagnostic_camera_t;

void rtmp_diagnostics_track_camera(const rtmp_diagnostic_camera_t *camera);

#endif // RTMP_DIAGNOSTICS_H