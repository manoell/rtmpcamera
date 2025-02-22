#include "rtmp_quality.h"
#include "rtmp_utils.h"
#include "rtmp_diagnostics.h"
#include <string.h>
#include <math.h>
#include <pthread.h>

#define RTMP_QUALITY_CHECK_INTERVAL 1000  // Check every 1 second
#define RTMP_QUALITY_HISTORY_SIZE 30      // Keep 30 seconds of history
#define RTMP_MIN_BITRATE 100000           // 100 Kbps
#define RTMP_MAX_BITRATE 10000000         // 10 Mbps
#define RTMP_MIN_FPS 10
#define RTMP_MAX_FPS 60

typedef struct {
    float network_quality;
    float processing_quality;
    float overall_quality;
    uint64_t timestamp;
} quality_sample_t;

struct rtmp_quality_controller {
    pthread_mutex_t mutex;
    quality_sample_t history[RTMP_QUALITY_HISTORY_SIZE];
    size_t history_index;
    rtmp_quality_config_t config;
    rtmp_quality_stats_t stats;
    rtmp_quality_callbacks_t callbacks;
    void *user_data;
    uint64_t last_check_time;
    int active;
};

// Create quality controller
rtmp_quality_controller_t* rtmp_quality_create(const rtmp_quality_config_t *config) {
    rtmp_quality_controller_t *controller = calloc(1, sizeof(rtmp_quality_controller_t));
    if (!controller) {
        rtmp_log_error("Failed to allocate quality controller");
        return NULL;
    }
    
    if (pthread_mutex_init(&controller->mutex, NULL) != 0) {
        free(controller);
        return NULL;
    }
    
    // Set default configuration if none provided
    if (config) {
        memcpy(&controller->config, config, sizeof(rtmp_quality_config_t));
    } else {
        controller->config.target_quality = 1.0f;
        controller->config.min_quality = 0.1f;
        controller->config.adjust_threshold = 0.1f;
        controller->config.network_weight = 0.6f;
        controller->config.processing_weight = 0.4f;
    }
    
    controller->last_check_time = rtmp_utils_get_time_ms();
    
    return controller;
}

// Update network metrics
void rtmp_quality_update_network(rtmp_quality_controller_t *controller,
                               const rtmp_network_metrics_t *metrics) {
    if (!controller || !metrics) return;
    
    pthread_mutex_lock(&controller->mutex);
    
    // Calculate network quality score (0.0 - 1.0)
    float latency_score = metrics->latency > 0 ? 
        1.0f - fminf(metrics->latency / 1000.0f, 1.0f) : 1.0f;
    
    float packet_loss_score = 1.0f - metrics->packet_loss_rate;
    
    float bandwidth_score = metrics->available_bandwidth > 0 ?
        fminf(metrics->current_bitrate / (float)metrics->available_bandwidth, 1.0f) : 1.0f;
    
    // Weighted average of metrics
    float network_quality = latency_score * 0.4f + 
                          packet_loss_score * 0.4f +
                          bandwidth_score * 0.2f;
    
    // Update current sample
    controller->history[controller->history_index].network_quality = network_quality;
    controller->history[controller->history_index].timestamp = rtmp_utils_get_time_ms();
    
    // Update statistics
    controller->stats.current_latency = metrics->latency;
    controller->stats.packet_loss_rate = metrics->packet_loss_rate;
    controller->stats.current_bitrate = metrics->current_bitrate;
    controller->stats.available_bandwidth = metrics->available_bandwidth;
    
    pthread_mutex_unlock(&controller->mutex);
}

// Update processing metrics
void rtmp_quality_update_processing(rtmp_quality_controller_t *controller,
                                  const rtmp_processing_metrics_t *metrics) {
    if (!controller || !metrics) return;
    
    pthread_mutex_lock(&controller->mutex);
    
    // Calculate processing quality score (0.0 - 1.0)
    float frame_drop_score = 1.0f - fminf(metrics->frame_drop_rate, 1.0f);
    float processing_delay_score = metrics->processing_delay > 0 ?
        1.0f - fminf(metrics->processing_delay / 100.0f, 1.0f) : 1.0f;
    float cpu_usage_score = 1.0f - fminf(metrics->cpu_usage / 100.0f, 1.0f);
    
    // Weighted average of metrics
    float processing_quality = frame_drop_score * 0.4f +
                             processing_delay_score * 0.3f +
                             cpu_usage_score * 0.3f;
    
    // Update current sample
    controller->history[controller->history_index].processing_quality = processing_quality;
    
    // Calculate overall quality
    float network_quality = controller->history[controller->history_index].network_quality;
    float overall_quality = network_quality * controller->config.network_weight +
                           processing_quality * controller->config.processing_weight;
    
    controller->history[controller->history_index].overall_quality = overall_quality;
    
    // Update index
    controller->history_index = (controller->history_index + 1) % RTMP_QUALITY_HISTORY_SIZE;
    
    // Update statistics
    controller->stats.frame_drop_rate = metrics->frame_drop_rate;
    controller->stats.processing_delay = metrics->processing_delay;
    controller->stats.cpu_usage = metrics->cpu_usage;
    controller->stats.current_quality = overall_quality;
    
    // Check if quality adjustment is needed
    uint64_t current_time = rtmp_utils_get_time_ms();
    if (current_time - controller->last_check_time >= RTMP_QUALITY_CHECK_INTERVAL) {
        rtmp_quality_check_adjust(controller);
        controller->last_check_time = current_time;
    }
    
    pthread_mutex_unlock(&controller->mutex);
}

// Check and adjust quality if needed
static void rtmp_quality_check_adjust(rtmp_quality_controller_t *controller) {
    // Calculate average quality over history
    float sum_quality = 0;
    int valid_samples = 0;
    
    for (int i = 0; i < RTMP_QUALITY_HISTORY_SIZE; i++) {
        if (controller->history[i].timestamp > 0) {
            sum_quality += controller->history[i].overall_quality;
            valid_samples++;
        }
    }
    
    if (valid_samples == 0) return;
    
    float avg_quality = sum_quality / valid_samples;
    float quality_diff = fabs(avg_quality - controller->config.target_quality);
    
    // Check if adjustment is needed
    if (quality_diff > controller->config.adjust_threshold) {
        // Calculate new quality target
        float new_quality = avg_quality < controller->config.target_quality ?
            fmaxf(avg_quality - controller->config.adjust_threshold, controller->config.min_quality) :
            fminf(avg_quality + controller->config.adjust_threshold, 1.0f);
        
        // Calculate new parameters
        rtmp_quality_params_t new_params;
        new_params.bitrate = (int)(RTMP_MAX_BITRATE * new_quality);
        new_params.fps = (int)(RTMP_MIN_FPS + (RTMP_MAX_FPS - RTMP_MIN_FPS) * new_quality);
        new_params.quality = new_quality;
        
        // Ensure minimum values
        if (new_params.bitrate < RTMP_MIN_BITRATE) new_params.bitrate = RTMP_MIN_BITRATE;
        if (new_params.fps < RTMP_MIN_FPS) new_params.fps = RTMP_MIN_FPS;
        
        // Notify through callback
        if (controller->callbacks.quality_adjusted) {
            controller->callbacks.quality_adjusted(&new_params, controller->user_data);
        }
        
        // Update statistics
        controller->stats.quality_adjustments++;
        controller->stats.last_adjustment_time = rtmp_utils_get_time_ms();
    }
}

// Set callbacks
void rtmp_quality_set_callbacks(rtmp_quality_controller_t *controller,
                              const rtmp_quality_callbacks_t *callbacks,
                              void *user_data) {
    if (!controller || !callbacks) return;
    
    pthread_mutex_lock(&controller->mutex);
    memcpy(&controller->callbacks, callbacks, sizeof(rtmp_quality_callbacks_t));
    controller->user_data = user_data;
    pthread_mutex_unlock(&controller->mutex);
}

// Get current statistics
const rtmp_quality_stats_t* rtmp_quality_get_stats(rtmp_quality_controller_t *controller) {
    if (!controller) return NULL;
    return &controller->stats;
}

// Reset statistics
void rtmp_quality_reset_stats(rtmp_quality_controller_t *controller) {
    if (!controller) return;
    
    pthread_mutex_lock(&controller->mutex);
    memset(&controller->stats, 0, sizeof(rtmp_quality_stats_t));
    controller->stats.start_time = rtmp_utils_get_time_ms();
    pthread_mutex_unlock(&controller->mutex);
}

// Destroy controller
void rtmp_quality_destroy(rtmp_quality_controller_t *controller) {
    if (!controller) return;
    
    pthread_mutex_destroy(&controller->mutex);
    free(controller);
}

// Debug functions
void rtmp_quality_dump_debug_info(rtmp_quality_controller_t *controller) {
    if (!controller) return;
    
    pthread_mutex_lock(&controller->mutex);
    
    rtmp_log_debug("=== Quality Controller Debug Info ===");
    rtmp_log_debug("Current Quality: %.2f", controller->stats.current_quality);
    rtmp_log_debug("Network Metrics:");
    rtmp_log_debug("  Latency: %lu ms", controller->stats.current_latency);
    rtmp_log_debug("  Packet Loss: %.2f%%", controller->stats.packet_loss_rate * 100);
    rtmp_log_debug("  Bitrate: %lu bps", controller->stats.current_bitrate);
    rtmp_log_debug("  Available Bandwidth: %lu bps", controller->stats.available_bandwidth);
    
    rtmp_log_debug("Processing Metrics:");
    rtmp_log_debug("  Frame Drop Rate: %.2f%%", controller->stats.frame_drop_rate * 100);
    rtmp_log_debug("  Processing Delay: %.2f ms", controller->stats.processing_delay);
    rtmp_log_debug("  CPU Usage: %.2f%%", controller->stats.cpu_usage);
    
    rtmp_log_debug("Quality Adjustments: %lu", controller->stats.quality_adjustments);
    
    pthread_mutex_unlock(&controller->mutex);
}