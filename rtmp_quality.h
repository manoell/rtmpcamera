#ifndef RTMP_QUALITY_H
#define RTMP_QUALITY_H

#include <stdint.h>

// Quality controller handle
typedef struct rtmp_quality_controller rtmp_quality_controller_t;

// Network metrics
typedef struct {
    uint64_t latency;           // Current latency in milliseconds
    float packet_loss_rate;     // Packet loss rate (0.0 - 1.0)
    uint64_t current_bitrate;   // Current bitrate in bps
    uint64_t available_bandwidth; // Available bandwidth in bps
} rtmp_network_metrics_t;

// Processing metrics
typedef struct {
    float frame_drop_rate;      // Frame drop rate (0.0 - 1.0)
    float processing_delay;     // Processing delay in milliseconds
    float cpu_usage;           // CPU usage percentage (0.0 - 100.0)
} rtmp_processing_metrics_t;

// Quality parameters
typedef struct {
    int bitrate;              // Target bitrate in bps
    int fps;                  // Target FPS
    float quality;           // Overall quality factor (0.0 - 1.0)
} rtmp_quality_params_t;

// Configuration
typedef struct {
    float target_quality;     // Target quality level (0.0 - 1.0)
    float min_quality;        // Minimum acceptable quality
    float adjust_threshold;   // Threshold for quality adjustment
    float network_weight;     // Weight for network metrics
    float processing_weight;  // Weight for processing metrics
} rtmp_quality_config_t;

// Statistics
typedef struct {
    uint64_t start_time;
    uint64_t current_latency;
    float packet_loss_rate;
    uint64_t current_bitrate;
    uint64_t available_bandwidth;
    float frame_drop_rate;
    float processing_delay;
    float cpu_usage;
    float current_quality;
    uint64_t quality_adjustments;
    uint64_t last_adjustment_time;
} rtmp_quality_stats_t;

// Callbacks
typedef struct {
    void (*quality_adjusted)(const rtmp_quality_params_t *params, void *user_data);
} rtmp_quality_callbacks_t;

// Core functions
rtmp_quality_controller_t* rtmp_quality_create(const rtmp_quality_config_t *config);
void rtmp_quality_destroy(rtmp_quality_controller_t *controller);

// Metrics update
void rtmp_quality_update_network(rtmp_quality_controller_t *controller,
                               const rtmp_network_metrics_t *metrics);
void rtmp_quality_update_processing(rtmp_quality_controller_t *controller,
                                  const rtmp_processing_metrics_t *metrics);

// Configuration and callbacks
void rtmp_quality_set_callbacks(rtmp_quality_controller_t *controller,
                              const rtmp_quality_callbacks_t *callbacks,
                              void *user_data);

// Statistics
const rtmp_quality_stats_t* rtmp_quality_get_stats(rtmp_quality_controller_t *controller);
void rtmp_quality_reset_stats(rtmp_quality_controller_t *controller);

// Debug functions
void rtmp_quality_dump_debug_info(rtmp_quality_controller_t *controller);

#endif // RTMP_QUALITY_H