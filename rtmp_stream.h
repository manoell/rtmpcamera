#ifndef RTMP_STREAM_H
#define RTMP_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// Forward declaration
typedef struct RTMPStream RTMPStream;

// Event types for stream callbacks
typedef enum {
    RTMP_EVENT_CONNECTED,
    RTMP_EVENT_DISCONNECTED,
    RTMP_EVENT_FRAME_PROCESSED,
    RTMP_EVENT_ERROR
} RTMPStreamEventType;

// Stream statistics structure
typedef struct {
    uint32_t current_fps;
    uint32_t current_bitrate;
    uint32_t buffer_ms;
    uint32_t quality_percent;
    uint32_t dropped_frames;
} RTMPStreamStats;

// Event structure for callbacks
typedef struct {
    RTMPStreamEventType type;
    int64_t timestamp;
    const uint8_t *data;
    size_t data_size;
    const char *error_message;  // Only valid for RTMP_EVENT_ERROR
} RTMPStreamEvent;

// Callback function type
typedef void (*rtmp_stream_callback_t)(const RTMPStreamEvent *event, void *context);

// Stream management functions
RTMPStream* rtmp_stream_create(const char *url);
void rtmp_stream_destroy(RTMPStream *stream);
bool rtmp_stream_start(RTMPStream *stream);
void rtmp_stream_stop(RTMPStream *stream);

// Frame and data handling
bool rtmp_stream_push_frame(RTMPStream *stream, const uint8_t *data, size_t size, 
                          int64_t timestamp, bool is_keyframe);
void rtmp_stream_set_callback(RTMPStream *stream, rtmp_stream_callback_t callback, 
                            void *context);

// Configuration and status
bool rtmp_stream_set_video_params(RTMPStream *stream, uint32_t width, uint32_t height, 
                                uint32_t fps, uint32_t bitrate);
void rtmp_stream_get_stats(RTMPStream *stream, RTMPStreamStats *stats);
bool rtmp_stream_is_connected(RTMPStream *stream);
void rtmp_stream_set_connected(RTMPStream *stream, bool connected);

#endif // RTMP_STREAM_H