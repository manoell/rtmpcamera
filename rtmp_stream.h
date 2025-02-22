#ifndef RTMP_STREAM_H
#define RTMP_STREAM_H

#include <stdint.h>
#include <stddef.h>

// Stream handle
typedef struct rtmp_stream rtmp_stream_t;

// Error codes
#define RTMP_SUCCESS 0
#define RTMP_ERROR_INVALID_PARAM -1
#define RTMP_ERROR_MEMORY -2
#define RTMP_ERROR_ALREADY_RUNNING -3
#define RTMP_ERROR_NOT_RUNNING -4
#define RTMP_ERROR_THREAD_CREATE -5
#define RTMP_ERROR_NO_CALLBACK -6
#define RTMP_ERROR_FRAME_TOO_LARGE -7

// Health status flags
#define RTMP_HEALTH_OK 0
#define RTMP_HEALTH_HIGH_LATENCY (1 << 0)
#define RTMP_HEALTH_HIGH_DROP_RATE (1 << 1)
#define RTMP_HEALTH_HIGH_FAILURE_RATE (1 << 2)
#define RTMP_HEALTH_LOW_QUALITY (1 << 3)

// Stream configuration
typedef struct {
    int width;
    int height;
    int fps;
    int bitrate;
    int gop_size;
    float quality;
} rtmp_stream_config_t;

// Stream statistics
typedef struct {
    uint64_t start_time;
    uint64_t uptime;
    uint64_t total_frames;
    uint64_t processed_frames;
    uint64_t dropped_frames;
    uint64_t failed_frames;
    uint64_t keyframes;
    uint64_t bytes_received;
    uint64_t bytes_sent;
    uint64_t current_latency;
    uint64_t max_latency;
    uint64_t average_bitrate;
    float current_quality;
} rtmp_stream_stats_t;

// Stream callbacks
typedef struct {
    int (*process_frame)(const uint8_t *data, size_t size, uint64_t timestamp, void *user_data);
    int (*request_keyframe)(void *user_data);
    void (*quality_changed)(float quality, const rtmp_stream_config_t *config, void *user_data);
} rtmp_stream_callbacks_t;

// Core functions
rtmp_stream_t* rtmp_stream_create(const rtmp_stream_config_t *config);
void rtmp_stream_destroy(rtmp_stream_t *stream);
int rtmp_stream_start(rtmp_stream_t *stream);
int rtmp_stream_stop(rtmp_stream_t *stream);

// Frame handling
int rtmp_stream_push_frame(rtmp_stream_t *stream, const uint8_t *data, size_t size, 
                          uint64_t timestamp, int is_keyframe);
int rtmp_stream_request_keyframe(rtmp_stream_t *stream);

// Configuration and callbacks
void rtmp_stream_set_callbacks(rtmp_stream_t *stream, const rtmp_stream_callbacks_t *callbacks,
                             void *user_data);
int rtmp_stream_set_config(rtmp_stream_t *stream, const rtmp_stream_config_t *config);
int rtmp_stream_get_config(rtmp_stream_t *stream, rtmp_stream_config_t *config);

// Statistics and monitoring
const rtmp_stream_stats_t* rtmp_stream_get_stats(rtmp_stream_t *stream);
float rtmp_stream_get_quality(rtmp_stream_t *stream);
void rtmp_stream_reset_stats(rtmp_stream_t *stream);
int rtmp_stream_health_check(rtmp_stream_t *stream);

// Buffer management
int rtmp_stream_clear_buffer(rtmp_stream_t *stream);
int rtmp_stream_is_active(rtmp_stream_t *stream);

// Debug functions
void rtmp_stream_dump_debug_info(rtmp_stream_t *stream);

#endif // RTMP_STREAM_H