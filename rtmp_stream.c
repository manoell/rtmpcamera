#include "rtmp_stream.h"
#include "rtmp_utils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Private definitions
#define DEFAULT_BUFFER_SIZE (512 * 1024)  // 512KB
#define MIN_BITRATE 100000                // 100 Kbps
#define MAX_BITRATE 8000000               // 8 Mbps
#define STATS_UPDATE_INTERVAL 1000        // 1 second
#define QUALITY_CHECK_INTERVAL 5000       // 5 seconds

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} StreamBuffer;

typedef struct {
    StreamBuffer video;
    StreamBuffer audio;
    uint32_t bufferSize;
    uint32_t maxBitrate;
    uint32_t minBitrate;
    bool adaptiveBitrate;
    RTMPStreamQuality quality;
    uint32_t lastQualityCheck;
    uint32_t lastStatsUpdate;
} StreamContext;

// Private helper functions
static bool send_metadata(RTMPStream *stream);
static void update_stats(RTMPStream *stream, uint32_t now);
static void check_quality(RTMPStream *stream, uint32_t now);
static bool handle_connect_response(RTMPStream *stream, const AMFObject *response);
static bool handle_publish_response(RTMPStream *stream, const AMFObject *response);
static void reset_stream_context(StreamContext *ctx);

// Implementation
RTMPStream *rtmp_stream_create(RTMPContext *rtmp) {
    if (!rtmp) return NULL;

    RTMPStream *stream = (RTMPStream *)calloc(1, sizeof(RTMPStream));
    if (!stream) return NULL;

    StreamContext *ctx = (StreamContext *)calloc(1, sizeof(StreamContext));
    if (!ctx) {
        free(stream);
        return NULL;
    }

    stream->rtmp = rtmp;
    stream->state = RTMP_STREAM_STATE_IDLE;
    stream->userData = ctx;

    // Initialize default config
    stream->config.width = 1280;
    stream->config.height = 720;
    stream->config.frameRate = 30;
    stream->config.videoBitrate = 2000000;  // 2 Mbps
    stream->config.audioBitrate = 128000;   // 128 Kbps
    stream->config.enableAudio = true;
    stream->config.enableVideo = true;

    // Initialize context
    ctx->bufferSize = DEFAULT_BUFFER_SIZE;
    ctx->maxBitrate = MAX_BITRATE;
    ctx->minBitrate = MIN_BITRATE;
    ctx->quality = RTMP_QUALITY_HIGH;
    ctx->adaptiveBitrate = true;

    return stream;
}

void rtmp_stream_destroy(RTMPStream *stream) {
    if (!stream) return;

    StreamContext *ctx = (StreamContext *)stream->userData;
    if (ctx) {
        if (ctx->video.data) free(ctx->video.data);
        if (ctx->audio.data) free(ctx->audio.data);
        free(ctx);
    }

    free(stream);
}

bool rtmp_stream_connect(RTMPStream *stream, const char *url) {
    if (!stream || !url) return false;

    // Parse URL
    char host[256];
    int port = RTMP_DEFAULT_PORT;
    char app[128];
    char streamName[128];
    
    // Simple URL parser - in production you'd want more robust parsing
    if (sscanf(url, "rtmp://%255[^:]:%d/%127[^/]/%127s", 
               host, &port, app, streamName) < 4) {
        if (sscanf(url, "rtmp://%255[^/]/%127[^/]/%127s",
                   host, app, streamName) < 3) {
            rtmp_log(RTMP_LOG_ERROR, "Invalid RTMP URL format");
            return false;
        }
    }

    stream->state = RTMP_STREAM_STATE_CONNECTING;
    strncpy(stream->streamName, streamName, sizeof(stream->streamName) - 1);

    // Configure RTMP context
    RTMPContext *rtmp = stream->rtmp;
    strncpy(rtmp->settings.app, app, sizeof(rtmp->settings.app) - 1);
    snprintf(rtmp->settings.tcUrl, sizeof(rtmp->settings.tcUrl),
             "rtmp://%s:%d/%s", host, port, app);

    // Connect to server
    if (!rtmp_connect(rtmp, host, port)) {
        stream->state = RTMP_STREAM_STATE_IDLE;
        return false;
    }

    // Send connect command
    if (!rtmp_send_connect(rtmp)) {
        rtmp_disconnect(rtmp);
        stream->state = RTMP_STREAM_STATE_IDLE;
        return false;
    }

    return true;
}

void rtmp_stream_disconnect(RTMPStream *stream) {
    if (!stream) return;

    if (stream->state != RTMP_STREAM_STATE_IDLE) {
        rtmp_disconnect(stream->rtmp);
        stream->state = RTMP_STREAM_STATE_IDLE;
    }

    StreamContext *ctx = (StreamContext *)stream->userData;
    if (ctx) {
        reset_stream_context(ctx);
    }

    if (stream->onStateChange) {
        stream->onStateChange(stream, RTMP_STREAM_STATE_IDLE);
    }
}

bool rtmp_stream_is_connected(RTMPStream *stream) {
    return stream && stream->state != RTMP_STREAM_STATE_IDLE && 
           stream->state != RTMP_STREAM_STATE_CLOSED;
}

bool rtmp_stream_publish(RTMPStream *stream, const char *name) {
    if (!stream || !name || !rtmp_stream_is_connected(stream)) return false;

    // Create stream first
    if (!rtmp_send_create_stream(stream->rtmp)) {
        return false;
    }

    // Send publish command
    RTMPContext *rtmp = stream->rtmp;
    strncpy(stream->streamName, name, sizeof(stream->streamName) - 1);
    
    if (!rtmp_send_publish(rtmp)) {
        return false;
    }

    stream->state = RTMP_STREAM_STATE_PUBLISHING;
    
    // Send metadata
    if (!send_metadata(stream)) {
        rtmp_log(RTMP_LOG_WARNING, "Failed to send stream metadata");
    }

    if (stream->onStateChange) {
        stream->onStateChange(stream, RTMP_STREAM_STATE_PUBLISHING);
    }

    return true;
}

bool rtmp_stream_send_video(RTMPStream *stream, const uint8_t *data, size_t size,
                          uint32_t timestamp, bool keyframe) {
    if (!stream || !data || !size) return false;
    if (stream->state != RTMP_STREAM_STATE_PUBLISHING) return false;

    RTMPPacket packet = {
        .type = RTMP_MSG_VIDEO,
        .timestamp = timestamp,
        .streamId = stream->streamId,
        .data = (uint8_t *)data,
        .size = size
    };

    bool result = rtmp_send_packet(stream->rtmp, &packet);
    
    if (result) {
        // Update statistics
        stream->stats.videoFramesSent++;
        stream->stats.bytesSent += size;
        
        uint32_t now = rtmp_get_timestamp();
        update_stats(stream, now);
        
        if (stream->config.enableVideo && keyframe) {
            check_quality(stream, now);
        }
    }

    return result;
}

bool rtmp_stream_send_audio(RTMPStream *stream, const uint8_t *data, size_t size,
                          uint32_t timestamp) {
    if (!stream || !data || !size) return false;
    if (stream->state != RTMP_STREAM_STATE_PUBLISHING) return false;

    RTMPPacket packet = {
        .type = RTMP_MSG_AUDIO,
        .timestamp = timestamp,
        .streamId = stream->streamId,
        .data = (uint8_t *)data,
        .size = size
    };

    bool result = rtmp_send_packet(stream->rtmp, &packet);
    
    if (result) {
        stream->stats.audioFramesSent++;
        stream->stats.bytesSent += size;
        update_stats(stream, rtmp_get_timestamp());
    }

    return result;
}

static bool send_metadata(RTMPStream *stream) {
    AMFObject *metadata = amf_object_create();
    if (!metadata) return false;

    // Add video metadata
    amf_object_add_number(metadata, "width", stream->config.width);
    amf_object_add_number(metadata, "height", stream->config.height);
    amf_object_add_number(metadata, "videocodecid", stream->config.videoCodec);
    amf_object_add_number(metadata, "videodatarate", stream->config.videoBitrate / 1024.0);
    amf_object_add_number(metadata, "framerate", stream->config.frameRate);

    // Add audio metadata
    if (stream->config.enableAudio) {
        amf_object_add_number(metadata, "audiocodecid", stream->config.audioCodec);
        amf_object_add_number(metadata, "audiodatarate", stream->config.audioBitrate / 1024.0);
        amf_object_add_number(metadata, "audiochannels", 2);
    }

    // Add encoder metadata
    amf_object_add_string(metadata, "encoder", "rtmp_camera/1.0");
    amf_object_add_number(metadata, "filesize", 0);
    amf_object_add_bool(metadata, "hasAudio", stream->config.enableAudio);
    amf_object_add_bool(metadata, "hasVideo", stream->config.enableVideo);

    // Send metadata packet
    bool result = rtmp_stream_send_metadata(stream, "@setDataFrame", metadata);
    amf_object_free(metadata);

    return result;
}

bool rtmp_stream_send_metadata(RTMPStream *stream, const char *type, const AMFObject *metadata) {
    if (!stream || !type || !metadata) return false;

    uint8_t *data = NULL;
    size_t size = 0;

    // Encode metadata to AMF
    if (!amf_encode_metadata(type, metadata, &data, &size)) {
        return false;
    }

    RTMPPacket packet = {
        .type = RTMP_MSG_DATA_AMF0,
        .timestamp = 0,
        .streamId = stream->streamId,
        .data = data,
        .size = size
    };

    bool result = rtmp_send_packet(stream->rtmp, &packet);
    free(data);

    return result;
}

static void update_stats(RTMPStream *stream, uint32_t now) {
    if (!stream) return;

    StreamContext *ctx = (StreamContext *)stream->userData;
    if (!ctx) return;

    // Update stats every STATS_UPDATE_INTERVAL
    if (now - ctx->lastStatsUpdate < STATS_UPDATE_INTERVAL) {
        return;
    }

    // Calculate current bitrate
    uint32_t duration = now - ctx->lastStatsUpdate;
    if (duration > 0) {
        stream->stats.currentBitrate = (stream->stats.bytesSent * 8000) / duration; // bps
    }

    // Reset counters
    stream->stats.bytesSent = 0;
    ctx->lastStatsUpdate = now;

    // Update stream uptime
    stream->stats.streamUptime = now - stream->stats.streamUptime;
}

static void check_quality(RTMPStream *stream, uint32_t now) {
    if (!stream) return;

    StreamContext *ctx = (StreamContext *)stream->userData;
    if (!ctx || !ctx->adaptiveBitrate) return;

    // Check quality every QUALITY_CHECK_INTERVAL
    if (now - ctx->lastQualityCheck < QUALITY_CHECK_INTERVAL) {
        return;
    }

    // Simple adaptive bitrate logic
    uint32_t currentBitrate = stream->stats.currentBitrate;
    RTMPStreamQuality newQuality = ctx->quality;

    if (currentBitrate > ctx->maxBitrate * 0.9) {
        // If using >90% of max bitrate, reduce quality
        if (newQuality < RTMP_QUALITY_LOW) {
            newQuality++;
        }
    } else if (currentBitrate < ctx->maxBitrate * 0.7) {
        // If using <70% of max bitrate, increase quality
        if (newQuality > RTMP_QUALITY_HIGH) {
            newQuality--;
        }
    }

    if (newQuality != ctx->quality) {
        rtmp_stream_set_quality(stream, newQuality);
    }

    ctx->lastQualityCheck = now;
}

static void reset_stream_context(StreamContext *ctx) {
    if (!ctx) return;

    // Reset buffers
    if (ctx->video.data) {
        free(ctx->video.data);
        ctx->video.data = NULL;
    }
    ctx->video.size = 0;
    ctx->video.capacity = 0;

    if (ctx->audio.data) {
        free(ctx->audio.data);
        ctx->audio.data = NULL;
    }
    ctx->audio.size = 0;
    ctx->audio.capacity = 0;

    // Reset timing
    ctx->lastQualityCheck = 0;
    ctx->lastStatsUpdate = 0;
}

// Configuration functions
void rtmp_stream_set_video_config(RTMPStream *stream, uint32_t width, uint32_t height,
                                uint32_t frameRate, uint32_t bitrate) {
    if (!stream) return;

    stream->config.width = width;
    stream->config.height = height;
    stream->config.frameRate = frameRate;
    stream->config.videoBitrate = bitrate;

    // Send updated metadata if streaming
    if (stream->state == RTMP_STREAM_STATE_PUBLISHING) {
        send_metadata(stream);
    }
}

void rtmp_stream_set_audio_config(RTMPStream *stream, uint32_t sampleRate,
                                uint32_t channels, uint32_t bitrate) {
    if (!stream) return;

    stream->config.audioBitrate = bitrate;
    
    // Send updated metadata if streaming
    if (stream->state == RTMP_STREAM_STATE_PUBLISHING) {
        send_metadata(stream);
    }
}

void rtmp_stream_enable_audio(RTMPStream *stream, bool enable) {
    if (!stream) return;
    stream->config.enableAudio = enable;
}

void rtmp_stream_enable_video(RTMPStream *stream, bool enable) {
    if (!stream) return;
    stream->config.enableVideo = enable;
}

// Quality control functions
void rtmp_stream_set_quality(RTMPStream *stream, RTMPStreamQuality quality) {
    if (!stream) return;

    StreamContext *ctx = (StreamContext *)stream->userData;
    if (!ctx) return;

    ctx->quality = quality;

    // Adjust bitrate based on quality
    uint32_t newBitrate;
    switch (quality) {
        case RTMP_QUALITY_HIGH:
            newBitrate = ctx->maxBitrate;
            break;
        case RTMP_QUALITY_MEDIUM:
            newBitrate = (ctx->maxBitrate + ctx->minBitrate) / 2;
            break;
        case RTMP_QUALITY_LOW:
            newBitrate = ctx->minBitrate;
            break;
        case RTMP_QUALITY_AUTO:
            // Keep current bitrate, will be adjusted automatically
            return;
    }

    rtmp_stream_set_video_config(stream, stream->config.width, stream->config.height,
                                stream->config.frameRate, newBitrate);
}

void rtmp_stream_set_max_bitrate(RTMPStream *stream, uint32_t bitrate) {
    if (!stream) return;

    StreamContext *ctx = (StreamContext *)stream->userData;
    if (!ctx) return;

    ctx->maxBitrate = bitrate > MIN_BITRATE ? bitrate : MIN_BITRATE;
}

void rtmp_stream_set_min_bitrate(RTMPStream *stream, uint32_t bitrate) {
    if (!stream) return;

    StreamContext *ctx = (StreamContext *)stream->userData;
    if (!ctx) return;

    ctx->minBitrate = bitrate < MAX_BITRATE ? bitrate : MAX_BITRATE;
}

void rtmp_stream_enable_adaptive_bitrate(RTMPStream *stream, bool enable) {
    if (!stream) return;

    StreamContext *ctx = (StreamContext *)stream->userData;
    if (!ctx) return;

    ctx->adaptiveBitrate = enable;
}

// Statistics functions
RTMPStreamStats *rtmp_stream_get_stats(RTMPStream *stream) {
    if (!stream) return NULL;
    return &stream->stats;
}

void rtmp_stream_reset_stats(RTMPStream *stream) {
    if (!stream) return;
    memset(&stream->stats, 0, sizeof(RTMPStreamStats));
}

float rtmp_stream_get_bitrate(RTMPStream *stream) {
    if (!stream) return 0.0f;
    return stream->stats.currentBitrate / 1000.0f; // Return in Kbps
}

uint32_t rtmp_stream_get_fps(RTMPStream *stream) {
    if (!stream) return 0;
    
    // Calculate FPS over the last second
    uint32_t now = rtmp_get_timestamp();
    if (now - stream->stats.streamUptime >= 1000) {
        return stream->stats.videoFramesSent;
    }
    return 0;
}

// Buffer management functions
void rtmp_stream_set_buffer_size(RTMPStream *stream, uint32_t size) {
    if (!stream) return;

    StreamContext *ctx = (StreamContext *)stream->userData;
    if (!ctx) return;

    ctx->bufferSize = size;
}

uint32_t rtmp_stream_get_buffer_size(RTMPStream *stream) {
    if (!stream) return 0;

    StreamContext *ctx = (StreamContext *)stream->userData;
    if (!ctx) return 0;

    return ctx->bufferSize;
}

void rtmp_stream_clear_buffers(RTMPStream *stream) {
    if (!stream) return;

    StreamContext *ctx = (StreamContext *)stream->userData;
    if (!ctx) return;

    ctx->video.size = 0;
    ctx->audio.size = 0;
}