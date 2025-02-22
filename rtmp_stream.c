#include "rtmp_stream.h"
#include "rtmp_core.h"
#include "rtmp_utils.h"
#include "rtmp_diagnostics.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define RTMP_STREAM_BUFFER_SIZE (1024 * 1024)  // 1MB buffer
#define RTMP_MAX_FRAME_SIZE (1024 * 1024)      // 1MB max frame
#define RTMP_KEYFRAME_INTERVAL 30              // Request keyframe every 30 frames
#define RTMP_QUALITY_CHECK_INTERVAL 1000       // Check quality every 1 second
#define RTMP_MAX_LATENCY 2000                 // Maximum allowed latency in ms

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
    uint64_t timestamp;
} rtmp_frame_t;

typedef struct {
    rtmp_frame_t *frames;
    size_t head;
    size_t tail;
    size_t capacity;
    pthread_mutex_t mutex;
} rtmp_frame_buffer_t;

struct rtmp_stream {
    int active;
    pthread_mutex_t mutex;
    pthread_t process_thread;
    rtmp_frame_buffer_t frame_buffer;
    rtmp_stream_config_t config;
    rtmp_stream_stats_t stats;
    rtmp_stream_callbacks_t callbacks;
    uint64_t last_keyframe_time;
    uint64_t last_quality_check;
    float current_quality;
    void *user_data;
};

// Forward declarations
static void* rtmp_stream_process_loop(void *arg);
static int rtmp_stream_handle_frame(rtmp_stream_t *stream, rtmp_frame_t *frame);
static void rtmp_stream_check_quality(rtmp_stream_t *stream);
static void rtmp_stream_adjust_quality(rtmp_stream_t *stream, float quality);

// Create new stream
rtmp_stream_t* rtmp_stream_create(const rtmp_stream_config_t *config) {
    rtmp_stream_t *stream = (rtmp_stream_t*)calloc(1, sizeof(rtmp_stream_t));
    if (!stream) {
        rtmp_log_error("Failed to allocate stream");
        return NULL;
    }
    
    // Initialize mutex
    if (pthread_mutex_init(&stream->mutex, NULL) != 0) {
        free(stream);
        return NULL;
    }
    
    // Initialize frame buffer
    stream->frame_buffer.capacity = RTMP_STREAM_BUFFER_SIZE;
    stream->frame_buffer.frames = (rtmp_frame_t*)calloc(
        stream->frame_buffer.capacity, sizeof(rtmp_frame_t));
    
    if (!stream->frame_buffer.frames) {
        pthread_mutex_destroy(&stream->mutex);
        free(stream);
        return NULL;
    }
    
    if (pthread_mutex_init(&stream->frame_buffer.mutex, NULL) != 0) {
        free(stream->frame_buffer.frames);
        pthread_mutex_destroy(&stream->mutex);
        free(stream);
        return NULL;
    }
    
    // Copy configuration
    if (config) {
        memcpy(&stream->config, config, sizeof(rtmp_stream_config_t));
    } else {
        // Default configuration
        stream->config.width = 1280;
        stream->config.height = 720;
        stream->config.fps = 30;
        stream->config.bitrate = 2000000;  // 2 Mbps
        stream->config.gop_size = 30;
        stream->config.quality = 1.0f;
    }
    
    // Initialize statistics
    stream->stats.start_time = rtmp_utils_get_time_ms();
    stream->current_quality = 1.0f;
    
    return stream;
}

// Start stream processing
int rtmp_stream_start(rtmp_stream_t *stream) {
    if (!stream) return RTMP_ERROR_INVALID_PARAM;
    
    pthread_mutex_lock(&stream->mutex);
    
    if (stream->active) {
        pthread_mutex_unlock(&stream->mutex);
        return RTMP_ERROR_ALREADY_RUNNING;
    }
    
    stream->active = 1;
    
    // Start processing thread
    if (pthread_create(&stream->process_thread, NULL, rtmp_stream_process_loop, stream) != 0) {
        stream->active = 0;
        pthread_mutex_unlock(&stream->mutex);
        return RTMP_ERROR_THREAD_CREATE;
    }
    
    pthread_mutex_unlock(&stream->mutex);
    
    rtmp_log_info("Stream started");
    return RTMP_SUCCESS;
}

// Push frame to stream
int rtmp_stream_push_frame(rtmp_stream_t *stream, const uint8_t *data, size_t size, 
                          uint64_t timestamp, int is_keyframe) {
    if (!stream || !data || size == 0) return RTMP_ERROR_INVALID_PARAM;
    if (size > RTMP_MAX_FRAME_SIZE) return RTMP_ERROR_FRAME_TOO_LARGE;
    
    pthread_mutex_lock(&stream->frame_buffer.mutex);
    
    // Check buffer space
    size_t next_tail = (stream->frame_buffer.tail + 1) % stream->frame_buffer.capacity;
    if (next_tail == stream->frame_buffer.head) {
        // Buffer full, drop oldest frame
        stream->frame_buffer.head = (stream->frame_buffer.head + 1) % 
            stream->frame_buffer.capacity;
        stream->stats.dropped_frames++;
    }
    
    // Allocate frame data
    rtmp_frame_t *frame = &stream->frame_buffer.frames[stream->frame_buffer.tail];
    if (frame->capacity < size) {
        uint8_t *new_data = (uint8_t*)realloc(frame->data, size);
        if (!new_data) {
            pthread_mutex_unlock(&stream->frame_buffer.mutex);
            return RTMP_ERROR_MEMORY;
        }
        frame->data = new_data;
        frame->capacity = size;
    }
    
    // Copy frame data
    memcpy(frame->data, data, size);
    frame->size = size;
    frame->timestamp = timestamp;
    
    // Update buffer state
    stream->frame_buffer.tail = next_tail;
    
    // Update statistics
    stream->stats.total_frames++;
    stream->stats.bytes_received += size;
    if (is_keyframe) {
        stream->stats.keyframes++;
        stream->last_keyframe_time = timestamp;
    }
    
    pthread_mutex_unlock(&stream->frame_buffer.mutex);
    
    return RTMP_SUCCESS;
}

// Process frames in background thread
static void* rtmp_stream_process_loop(void *arg) {
    rtmp_stream_t *stream = (rtmp_stream_t*)arg;
    rtmp_frame_t frame_copy;
    memset(&frame_copy, 0, sizeof(frame_copy));
    
    while (stream->active) {
        int has_frame = 0;
        
        // Get next frame
        pthread_mutex_lock(&stream->frame_buffer.mutex);
        
        if (stream->frame_buffer.head != stream->frame_buffer.tail) {
            rtmp_frame_t *frame = &stream->frame_buffer.frames[stream->frame_buffer.head];
            
            // Make copy of frame data
            if (frame_copy.capacity < frame->size) {
                uint8_t *new_data = (uint8_t*)realloc(frame_copy.data, frame->size);
                if (new_data) {
                    frame_copy.data = new_data;
                    frame_copy.capacity = frame->size;
                }
            }
            
            if (frame_copy.capacity >= frame->size) {
                memcpy(frame_copy.data, frame->data, frame->size);
                frame_copy.size = frame->size;
                frame_copy.timestamp = frame->timestamp;
                has_frame = 1;
            }
            
            stream->frame_buffer.head = (stream->frame_buffer.head + 1) % 
                stream->frame_buffer.capacity;
        }
        
        pthread_mutex_unlock(&stream->frame_buffer.mutex);
        
        // Process frame if we got one
        if (has_frame) {
            if (rtmp_stream_handle_frame(stream, &frame_copy) != RTMP_SUCCESS) {
                stream->stats.failed_frames++;
            }
        }
        
        // Check and adjust quality periodically
        uint64_t current_time = rtmp_utils_get_time_ms();
        if (current_time - stream->last_quality_check >= RTMP_QUALITY_CHECK_INTERVAL) {
            rtmp_stream_check_quality(stream);
            stream->last_quality_check = current_time;
        }
        
        // Small sleep to prevent CPU overload
        rtmp_utils_sleep_ms(1);
    }
    
    // Cleanup
    free(frame_copy.data);
    return NULL;
}

// Handle single frame
static int rtmp_stream_handle_frame(rtmp_stream_t *stream, rtmp_frame_t *frame) {
    if (!stream->callbacks.process_frame) {
        return RTMP_ERROR_NO_CALLBACK;
    }
    
    // Calculate current latency
    uint64_t current_time = rtmp_utils_get_time_ms();
    uint64_t latency = current_time - frame->timestamp;
    
    // Update statistics
    stream->stats.current_latency = latency;
    if (latency > stream->stats.max_latency) {
        stream->stats.max_latency = latency;
    }
    
    // Process frame through callback
    int result = stream->callbacks.process_frame(frame->data, frame->size, 
                                               frame->timestamp, stream->user_data);
    
    if (result == RTMP_SUCCESS) {
        stream->stats.processed_frames++;
        stream->stats.bytes_sent += frame->size;
    }
    
    return result;
}

// Check stream quality
static void rtmp_stream_check_quality(rtmp_stream_t *stream) {
    float quality_score = 1.0f;
    
    // Calculate quality based on various metrics
    
    // 1. Latency impact
    if (stream->stats.current_latency > RTMP_MAX_LATENCY) {
        quality_score *= 0.8f;  // Reduce quality by 20% if latency is too high
    }
    
    // 2. Frame drop impact
    float drop_rate = (float)stream->stats.dropped_frames / stream->stats.total_frames;
    if (drop_rate > 0.1f) {  // More than 10% frames dropped
        quality_score *= (1.0f - drop_rate);
    }
    
    // 3. Processing failure impact
    float failure_rate = (float)stream->stats.failed_frames / stream->stats.total_frames;
    if (failure_rate > 0.05f) {  // More than 5% frames failed
        quality_score *= (1.0f - failure_rate);
    }
    
    // 4. Buffer utilization impact
    pthread_mutex_lock(&stream->frame_buffer.mutex);
    size_t buffer_used = (stream->frame_buffer.tail - stream->frame_buffer.head + 
                         stream->frame_buffer.capacity) % stream->frame_buffer.capacity;
    float buffer_utilization = (float)buffer_used / stream->frame_buffer.capacity;
    pthread_mutex_unlock(&stream->frame_buffer.mutex);
    
    if (buffer_utilization > 0.9f) {  // Buffer more than 90% full
        quality_score *= 0.9f;
    }
    
    // Update quality if it changed significantly
    if (fabs(quality_score - stream->current_quality) > 0.1f) {
        rtmp_stream_adjust_quality(stream, quality_score);
    }
    
    // Update statistics
    stream->stats.current_quality = stream->current_quality;
}

// Adjust stream quality
static void rtmp_stream_adjust_quality(rtmp_stream_t *stream, float quality) {
    // Clamp quality between 0.1 and 1.0
    quality = quality < 0.1f ? 0.1f : (quality > 1.0f ? 1.0f : quality);
    
    if (quality == stream->current_quality) {
        return;
    }
    
    // Calculate new parameters based on quality
    rtmp_stream_config_t new_config = stream->config;
    new_config.bitrate = (int)(stream->config.bitrate * quality);
    new_config.fps = (int)(stream->config.fps * quality);
    
    // Ensure minimum values
    if (new_config.bitrate < 100000) new_config.bitrate = 100000;  // 100 Kbps minimum
    if (new_config.fps < 10) new_config.fps = 10;  // 10 FPS minimum
    
    // Notify quality change if callback is set
    if (stream->callbacks.quality_changed) {
        stream->callbacks.quality_changed(quality, &new_config, stream->user_data);
    }
    
    stream->current_quality = quality;
    memcpy(&stream->config, &new_config, sizeof(rtmp_stream_config_t));
    
    rtmp_log_info("Stream quality adjusted to %.2f", quality);
}

// Stop stream
int rtmp_stream_stop(rtmp_stream_t *stream) {
    if (!stream) return RTMP_ERROR_INVALID_PARAM;
    
    pthread_mutex_lock(&stream->mutex);
    
    if (!stream->active) {
        pthread_mutex_unlock(&stream->mutex);
        return RTMP_SUCCESS;
    }
    
    stream->active = 0;
    pthread_mutex_unlock(&stream->mutex);
    
    // Wait for processing thread to finish
    pthread_join(stream->process_thread, NULL);
    
    rtmp_log_info("Stream stopped");
    return RTMP_SUCCESS;
}

// Destroy stream
void rtmp_stream_destroy(rtmp_stream_t *stream) {
    if (!stream) return;
    
    rtmp_stream_stop(stream);
    
    // Cleanup frame buffer
    pthread_mutex_lock(&stream->frame_buffer.mutex);
    for (size_t i = 0; i < stream->frame_buffer.capacity; i++) {
        free(stream->frame_buffer.frames[i].data);
    }
    free(stream->frame_buffer.frames);
    pthread_mutex_unlock(&stream->frame_buffer.mutex);
    
    pthread_mutex_destroy(&stream->frame_buffer.mutex);
    pthread_mutex_destroy(&stream->mutex);
    
    free(stream);
}

// Set stream callbacks
void rtmp_stream_set_callbacks(rtmp_stream_t *stream, const rtmp_stream_callbacks_t *callbacks,
                             void *user_data) {
    if (!stream || !callbacks) return;
    
    pthread_mutex_lock(&stream->mutex);
    memcpy(&stream->callbacks, callbacks, sizeof(rtmp_stream_callbacks_t));
    stream->user_data = user_data;
    pthread_mutex_unlock(&stream->mutex);
}

// Get stream statistics
const rtmp_stream_stats_t* rtmp_stream_get_stats(rtmp_stream_t *stream) {
    if (!stream) return NULL;
    
    pthread_mutex_lock(&stream->mutex);
    // Update real-time stats
    stream->stats.uptime = rtmp_utils_get_time_ms() - stream->stats.start_time;
    if (stream->stats.uptime > 0) {
        stream->stats.average_bitrate = (stream->stats.bytes_sent * 8000) / stream->stats.uptime;  // in bps
    }
    pthread_mutex_unlock(&stream->mutex);
    
    return &stream->stats;
}

// Request keyframe
int rtmp_stream_request_keyframe(rtmp_stream_t *stream) {
    if (!stream) return RTMP_ERROR_INVALID_PARAM;
    if (!stream->callbacks.request_keyframe) return RTMP_ERROR_NO_CALLBACK;
    
    return stream->callbacks.request_keyframe(stream->user_data);
}

// Set stream configuration
int rtmp_stream_set_config(rtmp_stream_t *stream, const rtmp_stream_config_t *config) {
    if (!stream || !config) return RTMP_ERROR_INVALID_PARAM;
    
    pthread_mutex_lock(&stream->mutex);
    memcpy(&stream->config, config, sizeof(rtmp_stream_config_t));
    pthread_mutex_unlock(&stream->mutex);
    
    return RTMP_SUCCESS;
}

// Get stream configuration
int rtmp_stream_get_config(rtmp_stream_t *stream, rtmp_stream_config_t *config) {
    if (!stream || !config) return RTMP_ERROR_INVALID_PARAM;
    
    pthread_mutex_lock(&stream->mutex);
    memcpy(config, &stream->config, sizeof(rtmp_stream_config_t));
    pthread_mutex_unlock(&stream->mutex);
    
    return RTMP_SUCCESS;
}

// Clear stream buffer
int rtmp_stream_clear_buffer(rtmp_stream_t *stream) {
    if (!stream) return RTMP_ERROR_INVALID_PARAM;
    
    pthread_mutex_lock(&stream->frame_buffer.mutex);
    stream->frame_buffer.head = 0;
    stream->frame_buffer.tail = 0;
    pthread_mutex_unlock(&stream->frame_buffer.mutex);
    
    return RTMP_SUCCESS;
}

// Check if stream is active
int rtmp_stream_is_active(rtmp_stream_t *stream) {
    if (!stream) return 0;
    
    pthread_mutex_lock(&stream->mutex);
    int active = stream->active;
    pthread_mutex_unlock(&stream->mutex);
    
    return active;
}

// Get current stream quality
float rtmp_stream_get_quality(rtmp_stream_t *stream) {
    if (!stream) return 0.0f;
    
    pthread_mutex_lock(&stream->mutex);
    float quality = stream->current_quality;
    pthread_mutex_unlock(&stream->mutex);
    
    return quality;
}

// Reset stream statistics
void rtmp_stream_reset_stats(rtmp_stream_t *stream) {
    if (!stream) return;
    
    pthread_mutex_lock(&stream->mutex);
    memset(&stream->stats, 0, sizeof(rtmp_stream_stats_t));
    stream->stats.start_time = rtmp_utils_get_time_ms();
    pthread_mutex_unlock(&stream->mutex);
}

// Debug functions
void rtmp_stream_dump_debug_info(rtmp_stream_t *stream) {
    if (!stream) return;
    
    pthread_mutex_lock(&stream->mutex);
    
    rtmp_log_debug("=== RTMP Stream Debug Info ===");
    rtmp_log_debug("Active: %d", stream->active);
    rtmp_log_debug("Quality: %.2f", stream->current_quality);
    rtmp_log_debug("Configuration:");
    rtmp_log_debug("  Width: %d", stream->config.width);
    rtmp_log_debug("  Height: %d", stream->config.height);
    rtmp_log_debug("  FPS: %d", stream->config.fps);
    rtmp_log_debug("  Bitrate: %d bps", stream->config.bitrate);
    rtmp_log_debug("  GOP Size: %d", stream->config.gop_size);
    
    rtmp_log_debug("Statistics:");
    rtmp_log_debug("  Uptime: %lu ms", stream->stats.uptime);
    rtmp_log_debug("  Total Frames: %lu", stream->stats.total_frames);
    rtmp_log_debug("  Processed Frames: %lu", stream->stats.processed_frames);
    rtmp_log_debug("  Dropped Frames: %lu", stream->stats.dropped_frames);
    rtmp_log_debug("  Failed Frames: %lu", stream->stats.failed_frames);
    rtmp_log_debug("  Keyframes: %lu", stream->stats.keyframes);
    rtmp_log_debug("  Bytes Received: %lu", stream->stats.bytes_received);
    rtmp_log_debug("  Bytes Sent: %lu", stream->stats.bytes_sent);
    rtmp_log_debug("  Current Latency: %lu ms", stream->stats.current_latency);
    rtmp_log_debug("  Max Latency: %lu ms", stream->stats.max_latency);
    rtmp_log_debug("  Average Bitrate: %lu bps", stream->stats.average_bitrate);
    
    pthread_mutex_unlock(&stream->mutex);
}

// Health check
int rtmp_stream_health_check(rtmp_stream_t *stream) {
    if (!stream) return RTMP_ERROR_INVALID_PARAM;
    
    int health_status = RTMP_SUCCESS;
    
    pthread_mutex_lock(&stream->mutex);
    
    // Check various health indicators
    if (stream->stats.current_latency > RTMP_MAX_LATENCY * 2) {
        health_status |= RTMP_HEALTH_HIGH_LATENCY;
    }
    
    float drop_rate = (float)stream->stats.dropped_frames / stream->stats.total_frames;
    if (drop_rate > 0.2f) {
        health_status |= RTMP_HEALTH_HIGH_DROP_RATE;
    }
    
    float failure_rate = (float)stream->stats.failed_frames / stream->stats.total_frames;
    if (failure_rate > 0.1f) {
        health_status |= RTMP_HEALTH_HIGH_FAILURE_RATE;
    }
    
    if (stream->current_quality < 0.5f) {
        health_status |= RTMP_HEALTH_LOW_QUALITY;
    }
    
    pthread_mutex_unlock(&stream->mutex);
    
    return health_status;
}