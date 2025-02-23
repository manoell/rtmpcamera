#include "rtmp_quality.h"
#include "rtmp_utils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define QUALITY_CHECK_INTERVAL 5000 // 5 seconds
#define BUFFER_HEALTH_TARGET 3000   // 3 seconds
#define MAX_LATENCY 5000           // 5 seconds
#define MIN_KEYFRAME_INTERVAL 2000 // 2 seconds

struct RTMPQualityController {
    RTMPContext *rtmp;
    RTMPQualityConfig config;
    RTMPQualityStats stats;
    uint32_t lastCheck;
    uint32_t lastKeyframe;
    RTMPQualityCallback callback;
    void *userData;
};

// Private helper functions
static void adjust_quality(RTMPQualityController *ctrl);
static void apply_quality_level(RTMPQualityController *ctrl, RTMPQualityLevel level);
static uint32_t calculate_optimal_bitrate(RTMPQualityController *ctrl);
static uint32_t calculate_optimal_fps(RTMPQualityController *ctrl);
static bool should_increase_quality(RTMPQualityController *ctrl);
static bool should_decrease_quality(RTMPQualityController *ctrl);

RTMPQualityController *rtmp_quality_create(RTMPContext *rtmp) {
    if (!rtmp) return NULL;

    RTMPQualityController *ctrl = (RTMPQualityController *)calloc(1, sizeof(RTMPQualityController));
    if (!ctrl) return NULL;

    ctrl->rtmp = rtmp;
    
    // Set default configuration
    ctrl->config.level = RTMP_QUALITY_LEVEL_AUTO;
    ctrl->config.targetBitrate = RTMP_QUALITY_MEDIUM_BITRATE;
    ctrl->config.targetFPS = RTMP_QUALITY_MEDIUM_FPS;
    ctrl->config.keyframeInterval = 2000;
    ctrl->config.width = 1280;
    ctrl->config.height = 720;
    ctrl->config.adaptiveBitrate = true;
    ctrl->config.adaptiveFPS = true;

    return ctrl;
}

void rtmp_quality_destroy(RTMPQualityController *ctrl) {
    free(ctrl);
}

void rtmp_quality_set_level(RTMPQualityController *ctrl, RTMPQualityLevel level) {
    if (!ctrl) return;

    if (ctrl->config.level != level) {
        ctrl->config.level = level;
        apply_quality_level(ctrl, level);
    }
}

void rtmp_quality_set_target_bitrate(RTMPQualityController *ctrl, uint32_t bitrate) {
    if (!ctrl) return;
    ctrl->config.targetBitrate = bitrate;
}

void rtmp_quality_set_target_fps(RTMPQualityController *ctrl, uint32_t fps) {
    if (!ctrl) return;
    ctrl->config.targetFPS = fps;
}

void rtmp_quality_set_keyframe_interval(RTMPQualityController *ctrl, uint32_t interval) {
    if (!ctrl) return;
    ctrl->config.keyframeInterval = interval;
}

void rtmp_quality_set_resolution(RTMPQualityController *ctrl, uint32_t width, uint32_t height) {
    if (!ctrl) return;
    ctrl->config.width = width;
    ctrl->config.height = height;
}

void rtmp_quality_enable_adaptive_bitrate(RTMPQualityController *ctrl, bool enable) {
    if (!ctrl) return;
    ctrl->config.adaptiveBitrate = enable;
}

void rtmp_quality_enable_adaptive_fps(RTMPQualityController *ctrl, bool enable) {
    if (!ctrl) return;
    ctrl->config.adaptiveFPS = enable;
}

RTMPQualityStats *rtmp_quality_get_stats(RTMPQualityController *ctrl) {
    if (!ctrl) return NULL;
    return &ctrl->stats;
}

void rtmp_quality_reset_stats(RTMPQualityController *ctrl) {
    if (!ctrl) return;
    memset(&ctrl->stats, 0, sizeof(RTMPQualityStats));
}

void rtmp_quality_update_bitrate(RTMPQualityController *ctrl, uint32_t bytes, uint32_t duration) {
    if (!ctrl || !duration) return;
    ctrl->stats.currentBitrate = (bytes * 8 * 1000) / duration;
}

void rtmp_quality_update_fps(RTMPQualityController *ctrl, uint32_t frames, uint32_t duration) {
    if (!ctrl || !duration) return;
    ctrl->stats.currentFPS = (frames * 1000) / duration;
}

void rtmp_quality_add_dropped_frame(RTMPQualityController *ctrl) {
    if (!ctrl) return;
    ctrl->stats.droppedFrames++;
}

void rtmp_quality_add_keyframe(RTMPQualityController *ctrl) {
    if (!ctrl) return;
    ctrl->stats.keyframesSent++;
    ctrl->lastKeyframe = rtmp_get_timestamp();
}

void rtmp_quality_update_buffer(RTMPQualityController *ctrl, uint32_t size) {
    if (!ctrl) return;
    ctrl->stats.bufferHealth = size;
}

void rtmp_quality_update_timing(RTMPQualityController *ctrl, uint32_t encodeTime, uint32_t sendTime) {
    if (!ctrl) return;
    ctrl->stats.encodingTime = encodeTime;
    ctrl->stats.sendingTime = sendTime;
}

void rtmp_quality_update_latency(RTMPQualityController *ctrl, uint32_t latency) {
    if (!ctrl) return;
    ctrl->stats.latency = latency;
}

void rtmp_quality_check_and_adjust(RTMPQualityController *ctrl) {
    if (!ctrl) return;

    uint32_t now = rtmp_get_timestamp();
    if (now - ctrl->lastCheck < QUALITY_CHECK_INTERVAL) {
        return;
    }

    ctrl->lastCheck = now;

    if (ctrl->config.level == RTMP_QUALITY_LEVEL_AUTO) {
        adjust_quality(ctrl);
    }
}

bool rtmp_quality_should_drop_frame(RTMPQualityController *ctrl) {
    if (!ctrl) return false;

    if (ctrl->stats.bufferHealth > BUFFER_HEALTH_TARGET * 2) {
        return true;
    }

    if (ctrl->stats.currentFPS > ctrl->config.targetFPS * 1.1) {
        return true;
    }

    if (ctrl->stats.encodingTime + ctrl->stats.sendingTime > 1000/ctrl->config.targetFPS) {
        return true;
    }

    return false;
}

bool rtmp_quality_should_send_keyframe(RTMPQualityController *ctrl) {
    if (!ctrl) return false;

    uint32_t now = rtmp_get_timestamp();
    return (now - ctrl->lastKeyframe >= ctrl->config.keyframeInterval);
}

uint32_t rtmp_quality_get_target_bitrate(RTMPQualityController *ctrl) {
    if (!ctrl) return RTMP_QUALITY_MEDIUM_BITRATE;
    return ctrl->config.targetBitrate;
}

uint32_t rtmp_quality_get_target_fps(RTMPQualityController *ctrl) {
    if (!ctrl) return RTMP_QUALITY_MEDIUM_FPS;
    return ctrl->config.targetFPS;
}

void rtmp_quality_set_callback(RTMPQualityController *ctrl, RTMPQualityCallback callback, void *userData) {
    if (!ctrl) return;
    ctrl->callback = callback;
    ctrl->userData = userData;
}

static void adjust_quality(RTMPQualityController *ctrl) {
    if (!ctrl) return;

    RTMPQualityLevel newLevel = ctrl->config.level;

    if (should_decrease_quality(ctrl)) {
        if (newLevel > RTMP_QUALITY_LEVEL_LOW) {
            newLevel--;
        }
    } else if (should_increase_quality(ctrl)) {
        if (newLevel < RTMP_QUALITY_LEVEL_HIGH) {
            newLevel++;
        }
    }

    if (newLevel != ctrl->config.level) {
        apply_quality_level(ctrl, newLevel);
        
        if (ctrl->callback) {
            ctrl->callback(ctrl, newLevel, ctrl->userData);
        }
    }
}

static void apply_quality_level(RTMPQualityController *ctrl, RTMPQualityLevel level) {
    if (!ctrl) return;

    switch (level) {
        case RTMP_QUALITY_LEVEL_LOW:
            ctrl->config.targetBitrate = RTMP_QUALITY_LOW_BITRATE;
            ctrl->config.targetFPS = RTMP_QUALITY_LOW_FPS;
            break;

        case RTMP_QUALITY_LEVEL_MEDIUM:
            ctrl->config.targetBitrate = RTMP_QUALITY_MEDIUM_BITRATE;
            ctrl->config.targetFPS = RTMP_QUALITY_MEDIUM_FPS;
            break;

        case RTMP_QUALITY_LEVEL_HIGH:
            ctrl->config.targetBitrate = RTMP_QUALITY_HIGH_BITRATE;
            ctrl->config.targetFPS = RTMP_QUALITY_HIGH_FPS;
            break;

        case RTMP_QUALITY_LEVEL_AUTO:
            ctrl->config.targetBitrate = calculate_optimal_bitrate(ctrl);
            ctrl->config.targetFPS = calculate_optimal_fps(ctrl);
            break;
    }
}

static uint32_t calculate_optimal_bitrate(RTMPQualityController *ctrl) {
    if (!ctrl) return RTMP_QUALITY_MEDIUM_BITRATE;

    uint32_t optimalBitrate = ctrl->stats.currentBitrate;

    if (ctrl->stats.bufferHealth < BUFFER_HEALTH_TARGET) {
        optimalBitrate = (uint32_t)(optimalBitrate * 0.8);
    } else if (ctrl->stats.bufferHealth > BUFFER_HEALTH_TARGET * 2) {
        optimalBitrate = (uint32_t)(optimalBitrate * 1.2);
    }

    if (ctrl->stats.droppedFrames > 0) {
        optimalBitrate = (uint32_t)(optimalBitrate * 0.9);
    }

    if (ctrl->stats.latency > MAX_LATENCY) {
        optimalBitrate = (uint32_t)(optimalBitrate * 0.8);
    }

    // Clamp to limits
    if (optimalBitrate < RTMP_QUALITY_LOW_BITRATE) {
        optimalBitrate = RTMP_QUALITY_LOW_BITRATE;
    } else if (optimalBitrate > RTMP_QUALITY_HIGH_BITRATE) {
        optimalBitrate = RTMP_QUALITY_HIGH_BITRATE;
    }

    return optimalBitrate;
}

static uint32_t calculate_optimal_fps(RTMPQualityController *ctrl) {
    if (!ctrl) return RTMP_QUALITY_MEDIUM_FPS;

    uint32_t optimalFPS = ctrl->config.targetFPS;

    if (ctrl->stats.encodingTime > (1000 / optimalFPS)) {
        optimalFPS = (uint32_t)(optimalFPS * 0.8);
    }

    if (ctrl->stats.sendingTime > (1000 / optimalFPS)) {
        optimalFPS = (uint32_t)(optimalFPS * 0.8);
    }

    // Clamp to limits
    if (optimalFPS < RTMP_QUALITY_LOW_FPS) {
        optimalFPS = RTMP_QUALITY_LOW_FPS;
    } else if (optimalFPS > RTMP_QUALITY_HIGH_FPS) {
        optimalFPS = RTMP_QUALITY_HIGH_FPS;
    }

    return optimalFPS;
}

static bool should_decrease_quality(RTMPQualityController *ctrl) {
    if (!ctrl) return false;

    if (ctrl->stats.bufferHealth < BUFFER_HEALTH_TARGET / 2) return true;
    if (ctrl->stats.droppedFrames > ctrl->stats.currentFPS / 2) return true;
    if (ctrl->stats.latency > MAX_LATENCY * 1.5) return true;
    if (ctrl->stats.currentBitrate > ctrl->config.targetBitrate * 1.2) return true;
    if (ctrl->stats.encodingTime + ctrl->stats.sendingTime > 1000/ctrl->config.targetFPS) return true;

    return false;
}

static bool should_increase_quality(RTMPQualityController *ctrl) {
    if (!ctrl) return false;

    if (ctrl->stats.bufferHealth < BUFFER_HEALTH_TARGET) return false;
    if (ctrl->stats.droppedFrames > 0) return false;
    if (ctrl->stats.latency > MAX_LATENCY) return false;
    if (ctrl->stats.currentBitrate > ctrl->config.targetBitrate) return false;
    if (ctrl->stats.encodingTime + ctrl->stats.sendingTime > (1000/ctrl->config.targetFPS) * 0.8) return false;

    uint32_t now = rtmp_get_timestamp();
    if (now - ctrl->lastCheck < QUALITY_CHECK_INTERVAL * 2) return false;

    return true;
}