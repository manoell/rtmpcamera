#ifndef RTMP_STABILITY_H
#define RTMP_STABILITY_H

#include "rtmp_protocol.h"

// Recovery modes
typedef enum {
    RTMP_RECOVERY_MODE_NONE = 0,
    RTMP_RECOVERY_MODE_RECONNECT,
    RTMP_RECOVERY_MODE_RESET,
    RTMP_RECOVERY_MODE_FALLBACK
} RTMPRecoveryMode;

// Stability settings
typedef struct {
    uint32_t maxReconnectAttempts;
    uint32_t reconnectDelay;
    uint32_t heartbeatInterval;
    uint32_t watchdogTimeout;
    RTMPRecoveryMode recoveryMode;
    bool autoReconnect;
    bool useWatchdog;
    bool useHeartbeat;
} RTMPStabilityConfig;

// Stability monitor object
typedef struct RTMPStabilityMonitor RTMPStabilityMonitor;

// Creation/destruction
RTMPStabilityMonitor *rtmp_stability_create(RTMPContext *rtmp);
void rtmp_stability_destroy(RTMPStabilityMonitor *monitor);

// Configuration
void rtmp_stability_set_config(RTMPStabilityMonitor *monitor, const RTMPStabilityConfig *config);
const RTMPStabilityConfig *rtmp_stability_get_config(RTMPStabilityMonitor *monitor);

// Control functions
void rtmp_stability_start(RTMPStabilityMonitor *monitor);
void rtmp_stability_stop(RTMPStabilityMonitor *monitor);
void rtmp_stability_reset(RTMPStabilityMonitor *monitor);

// Status check
bool rtmp_stability_is_stable(RTMPStabilityMonitor *monitor);
bool rtmp_stability_is_recovering(RTMPStabilityMonitor *monitor);
uint32_t rtmp_stability_get_reconnect_count(RTMPStabilityMonitor *monitor);

// Manual recovery
bool rtmp_stability_try_recover(RTMPStabilityMonitor *monitor);
void rtmp_stability_force_reconnect(RTMPStabilityMonitor *monitor);

// Event notifications
typedef void (*RTMPStabilityCallback)(RTMPStabilityMonitor *monitor, RTMPRecoveryMode mode, void *userData);
void rtmp_stability_set_callback(RTMPStabilityMonitor *monitor, RTMPStabilityCallback callback, void *userData);

#endif /* RTMP_STABILITY_H */