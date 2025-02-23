#ifndef RTMP_QUALITY_H
#define RTMP_QUALITY_H

#include "rtmp_protocol.h"

// Quality settings
typedef enum {
    RTMP_QUALITY_LEVEL_LOW = 0,
    RTMP_QUALITY_LEVEL_MEDIUM,
    RTMP_QUALITY_LEVEL_HIGH,
    RTMP_QUALITY_LEVEL_AUTO
} RTMPQualityLevel;

// Quality thresholds
#define RTMP_QUALITY_LOW_BITRATE    400000  // 400 Kbps
#define RTMP_QUALITY_MEDIUM_BITRATE 1000000 // 1 Mbps
#define RTMP_QUALITY_HIGH_BITRATE   2500000 // 2.5 Mbps

#define RTMP_QUALITY_LOW_FPS    15
#define RTMP_QUALITY_MEDIUM_FPS 24
#define RTMP_QUALITY_HIGH_FPS   30

// Quality configuration
typedef struct {
    RTMPQualityLevel level;
    uint32_t targetBitrate;
    uint32_t targetFPS;
    uint32_t keyframeInterval;
    uint32_t width;
    uint32_t height;
    bool adaptiveBitrate;
    bool adaptiveFPS;
} RTMPQualityConfig;

// Quality statistics
typedef struct {
    uint32_t currentBitrate;
    uint32_t currentFPS;
    uint32_t droppedFrames;
    uint32_t keyframesSent;
    uint32_t bufferHealth;
    uint32_t encodingTime;
    uint32_t sendingTime;
    uint32_t latency;
} RTMPQualityStats;

// Quality controller object
typedef struct RTMPQualityController RTMPQualityController;

// Creation/destruction
RTMPQualityController *rtmp_quality_create(RTMPContext *rtmp);
void rtmp_quality_destroy(RTMPQualityController *ctrl);

// Configuration
void rtmp_quality_set_level(RTMPQualityController *ctrl, RTMPQualityLevel level);
void rtmp_quality_set_target_bitrate(RTMPQualityController *ctrl, uint32_t bitrate);
void rtmp_quality_set_target_fps(RTMPQualityController *ctrl, uint32_t fps);
void rtmp_quality_set_keyframe_interval(RTMPQualityController *ctrl, uint32_t interval);
void rtmp_quality_set_resolution(RTMPQualityController *ctrl, uint32_t width, uint32_t height);
void rtmp_quality_enable_adaptive_bitrate(RTMPQualityController *ctrl, bool enable);
void rtmp_quality_enable_adaptive_fps(RTMPQualityController *ctrl, bool enable);

// Monitoring
RTMPQualityStats *rtmp_quality_get_stats(RTMPQualityController *ctrl);
void rtmp_quality_reset_stats(RTMPQualityController *ctrl);
void rtmp_quality_update_bitrate(RTMPQualityController *ctrl, uint32_t bytes, uint32_t duration);
void rtmp_quality_update_fps(RTMPQualityController *ctrl, uint32_t frames, uint32_t duration);
void rtmp_quality_add_dropped_frame(RTMPQualityController *ctrl);
void rtmp_quality_add_keyframe(RTMPQualityController *ctrl);
void rtmp_quality_update_buffer(RTMPQualityController *ctrl, uint32_t size);
void rtmp_quality_update_timing(RTMPQualityController *ctrl, uint32_t encodeTime, uint32_t sendTime);
void rtmp_quality_update_latency(RTMPQualityController *ctrl, uint32_t latency);

// Quality adjustment
void rtmp_quality_check_and_adjust(RTMPQualityController *ctrl);
bool rtmp_quality_should_drop_frame(RTMPQualityController *ctrl);
bool rtmp_quality_should_send_keyframe(RTMPQualityController *ctrl);
uint32_t rtmp_quality_get_target_bitrate(RTMPQualityController *ctrl);
uint32_t rtmp_quality_get_target_fps(RTMPQualityController *ctrl);

// Notifications
typedef void (*RTMPQualityCallback)(RTMPQualityController *ctrl, RTMPQualityLevel newLevel, void *userData);
void rtmp_quality_set_callback(RTMPQualityController *ctrl, RTMPQualityCallback callback, void *userData);

#endif /* RTMP_QUALITY_H */