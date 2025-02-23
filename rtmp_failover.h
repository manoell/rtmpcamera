#ifndef RTMP_FAILOVER_H
#define RTMP_FAILOVER_H

#include "rtmp_protocol.h"

// Failover types
typedef enum {
    RTMP_FAILOVER_NONE = 0,
    RTMP_FAILOVER_SERVER,     // Switch to backup server
    RTMP_FAILOVER_NETWORK,    // Switch network interface
    RTMP_FAILOVER_LOCAL       // Switch to local recording
} RTMPFailoverType;

// Failover states
typedef enum {
    RTMP_FAILOVER_STATE_IDLE = 0,
    RTMP_FAILOVER_STATE_ACTIVE,
    RTMP_FAILOVER_STATE_SWITCHING,
    RTMP_FAILOVER_STATE_FAILED
} RTMPFailoverState;

// Failover settings
typedef struct {
    bool enableServerFailover;
    bool enableNetworkFailover;
    bool enableLocalFailover;
    uint32_t maxSwitchAttempts;
    uint32_t switchTimeout;
    uint32_t healthCheckInterval;
    char backupServers[8][256];
    uint32_t numBackupServers;
    char localRecordingPath[256];
} RTMPFailoverConfig;

// Failover status
typedef struct {
    RTMPFailoverState state;
    RTMPFailoverType currentType;
    uint32_t switchAttempts;
    uint32_t lastSwitchTime;
    uint32_t healthCheckTime;
    bool isHealthy;
    char currentServer[256];
    char currentNetwork[64];
} RTMPFailoverStatus;

// Failover handler object
typedef struct RTMPFailoverHandler RTMPFailoverHandler;

// Creation/destruction
RTMPFailoverHandler *rtmp_failover_create(RTMPContext *rtmp);
void rtmp_failover_destroy(RTMPFailoverHandler *handler);

// Configuration
void rtmp_failover_set_config(RTMPFailoverHandler *handler, const RTMPFailoverConfig *config);
const RTMPFailoverConfig *rtmp_failover_get_config(RTMPFailoverHandler *handler);

// Control
void rtmp_failover_start(RTMPFailoverHandler *handler);
void rtmp_failover_stop(RTMPFailoverHandler *handler);
void rtmp_failover_reset(RTMPFailoverHandler *handler);
bool rtmp_failover_trigger(RTMPFailoverHandler *handler, RTMPFailoverType type);

// Status
RTMPFailoverStatus *rtmp_failover_get_status(RTMPFailoverHandler *handler);
bool rtmp_failover_is_active(RTMPFailoverHandler *handler);
bool rtmp_failover_is_healthy(RTMPFailoverHandler *handler);

// Health check
void rtmp_failover_check_health(RTMPFailoverHandler *handler);
void rtmp_failover_set_healthy(RTMPFailoverHandler *handler, bool healthy);

// Callbacks
typedef void (*RTMPFailoverCallback)(RTMPFailoverHandler *handler, 
                                   RTMPFailoverType type,
                                   bool success,
                                   void *userData);

void rtmp_failover_set_callback(RTMPFailoverHandler *handler,
                               RTMPFailoverCallback callback,
                               void *userData);

#endif /* RTMP_FAILOVER_H */