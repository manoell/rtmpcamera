#ifndef RTMP_STREAM_H
#define RTMP_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include "rtmp_protocol.h"

// Stream states
typedef enum {
    RTMP_STREAM_STATE_IDLE = 0,
    RTMP_STREAM_STATE_CONNECTING,
    RTMP_STREAM_STATE_PUBLISHING,
    RTMP_STREAM_STATE_PLAYING,
    RTMP_STREAM_STATE_CLOSED
} RTMPStreamState;

// Stream settings
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t frameRate;
    uint32_t videoBitrate;
    uint32_t audioBitrate;
    bool enableAudio;
    bool enableVideo;
    uint8_t videoCodec;
    uint8_t audioCodec;
} RTMPStreamConfig;

// Stream statistics
typedef struct {
    uint32_t videoFramesSent;
    uint32_t audioFramesSent;
    uint32_t bytesSent;
    uint32_t currentBitrate;
    uint32_t droppedFrames;
    uint32_t avgEncodeTime;
    uint32_t avgSendTime;
    uint32_t streamUptime;
} RTMPStreamStats;

// Stream context
typedef struct RTMPStream {
    RTMPContext *rtmp;
    RTMPStreamState state;
    RTMPStreamConfig config;
    RTMPStreamStats stats;
    uint32_t streamId;
    char streamName[256];
    void *userData;

    // Callbacks
    void (*onStateChange)(struct RTMPStream *stream, RTMPStreamState state);
    void (*onError)(struct RTMPStream *stream, RTMPError error);
    void (*onMetaData)(struct RTMPStream *stream, const char *key, const AMFObject *value);
} RTMPStream;

// Core stream functions
RTMPStream *rtmp_stream_create(RTMPContext *rtmp);
void rtmp_stream_destroy(RTMPStream *stream);
bool rtmp_stream_connect(RTMPStream *stream, const char *url);
void rtmp_stream_disconnect(RTMPStream *stream);
bool rtmp_stream_is_connected(RTMPStream *stream);

// Publishing functions
bool rtmp_stream_publish(RTMPStream *stream, const char *name);
bool rtmp_stream_unpublish(RTMPStream *stream);
bool rtmp_stream_send_video(RTMPStream *stream, const uint8_t *data, size_t size, 
                          uint32_t timestamp, bool keyframe);
bool rtmp_stream_send_audio(RTMPStream *stream, const uint8_t *data, size_t size, 
                          uint32_t timestamp);
bool rtmp_stream_send_metadata(RTMPStream *stream, const char *type, 
                             const AMFObject *metadata);

// Playing functions
bool rtmp_stream_play(RTMPStream *stream, const char *name);
bool rtmp_stream_pause(RTMPStream *stream, bool pause);
bool rtmp_stream_seek(RTMPStream *stream, uint32_t timestamp);
bool rtmp_stream_stop(RTMPStream *stream);

// Configuration
void rtmp_stream_set_video_config(RTMPStream *stream, uint32_t width, uint32_t height,
                                uint32_t frameRate, uint32_t bitrate);
void rtmp_stream_set_audio_config(RTMPStream *stream, uint32_t sampleRate, 
                                uint32_t channels, uint32_t bitrate);
void rtmp_stream_enable_audio(RTMPStream *stream, bool enable);
void rtmp_stream_enable_video(RTMPStream *stream, bool enable);

// Statistics and monitoring
RTMPStreamStats *rtmp_stream_get_stats(RTMPStream *stream);
void rtmp_stream_reset_stats(RTMPStream *stream);
float rtmp_stream_get_bitrate(RTMPStream *stream);
uint32_t rtmp_stream_get_fps(RTMPStream *stream);

// Error handling
const char *rtmp_stream_get_error_string(RTMPError error);

// Buffer management
void rtmp_stream_set_buffer_size(RTMPStream *stream, uint32_t size);
uint32_t rtmp_stream_get_buffer_size(RTMPStream *stream);
void rtmp_stream_clear_buffers(RTMPStream *stream);

// Quality control
typedef enum {
    RTMP_QUALITY_HIGH = 0,
    RTMP_QUALITY_MEDIUM,
    RTMP_QUALITY_LOW,
    RTMP_QUALITY_AUTO
} RTMPStreamQuality;

void rtmp_stream_set_quality(RTMPStream *stream, RTMPStreamQuality quality);
void rtmp_stream_set_max_bitrate(RTMPStream *stream, uint32_t bitrate);
void rtmp_stream_set_min_bitrate(RTMPStream *stream, uint32_t bitrate);
void rtmp_stream_enable_adaptive_bitrate(RTMPStream *stream, bool enable);

#endif /* RTMP_STREAM_H */