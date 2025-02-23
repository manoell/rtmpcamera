#include "rtmp_stability.h"
#include "rtmp_utils.h"
#include <stdlib.h>
#include <string.h>

// Default settings
#define DEFAULT_RECONNECT_ATTEMPTS 3
#define DEFAULT_RECONNECT_DELAY 5000
#define DEFAULT_HEARTBEAT_INTERVAL 30000
#define DEFAULT_WATCHDOG_TIMEOUT 10000

typedef enum {
    MONITOR_STATE_STOPPED = 0,
    MONITOR_STATE_STARTING,
    MONITOR_STATE_RUNNING,
    MONITOR_STATE_RECOVERING,
    MONITOR_STATE_FAILED
} MonitorState;

struct RTMPStabilityMonitor {
    RTMPContext *rtmp;
    RTMPStabilityConfig config;
    MonitorState state;
    uint32_t reconnectCount;
    uint32_t lastHeartbeat;
    uint32_t lastResponse;
    RTMPStabilityCallback callback;
    void *userData;
    bool heartbeatPending;
    pthread_t watchdogThread;
    pthread_mutex_t mutex;
    bool watchdogRunning;
};

// Private helper functions
static void *watchdog_thread(void *arg);
static bool attempt_reconnect(RTMPStabilityMonitor *monitor);
static void send_heartbeat(RTMPStabilityMonitor *monitor);
static void check_connection_status(RTMPStabilityMonitor *monitor);
static void enter_recovery_mode(RTMPStabilityMonitor *monitor, RTMPRecoveryMode mode);
static void exit_recovery_mode(RTMPStabilityMonitor *monitor, bool success);

RTMPStabilityMonitor *rtmp_stability_create(RTMPContext *rtmp) {
    if (!rtmp) return NULL;

    RTMPStabilityMonitor *monitor = (RTMPStabilityMonitor *)calloc(1, sizeof(RTMPStabilityMonitor));
    if (!monitor) return NULL;

    monitor->rtmp = rtmp;
    monitor->state = MONITOR_STATE_STOPPED;
    
    // Initialize mutex
    pthread_mutex_init(&monitor->mutex, NULL);

    // Set default config
    monitor->config.maxReconnectAttempts = DEFAULT_RECONNECT_ATTEMPTS;
    monitor->config.reconnectDelay = DEFAULT_RECONNECT_DELAY;
    monitor->config.heartbeatInterval = DEFAULT_HEARTBEAT_INTERVAL;
    monitor->config.watchdogTimeout = DEFAULT_WATCHDOG_TIMEOUT;
    monitor->config.recoveryMode = RTMP_RECOVERY_MODE_RECONNECT;
    monitor->config.autoReconnect = true;
    monitor->config.useWatchdog = true;
    monitor->config.useHeartbeat = true;

    return monitor;
}

void rtmp_stability_destroy(RTMPStabilityMonitor *monitor) {
    if (!monitor) return;

    rtmp_stability_stop(monitor);
    pthread_mutex_destroy(&monitor->mutex);
    free(monitor);
}

void rtmp_stability_set_config(RTMPStabilityMonitor *monitor, const RTMPStabilityConfig *config) {
    if (!monitor || !config) return;

    pthread_mutex_lock(&monitor->mutex);
    memcpy(&monitor->config, config, sizeof(RTMPStabilityConfig));
    pthread_mutex_unlock(&monitor->mutex);
}

const RTMPStabilityConfig *rtmp_stability_get_config(RTMPStabilityMonitor *monitor) {
    if (!monitor) return NULL;
    return &monitor->config;
}

void rtmp_stability_start(RTMPStabilityMonitor *monitor) {
    if (!monitor || monitor->state != MONITOR_STATE_STOPPED) return;

    pthread_mutex_lock(&monitor->mutex);
    
    monitor->state = MONITOR_STATE_STARTING;
    monitor->reconnectCount = 0;
    monitor->lastHeartbeat = rtmp_get_timestamp();
    monitor->lastResponse = monitor->lastHeartbeat;
    monitor->heartbeatPending = false;

    // Start watchdog thread if enabled
    if (monitor->config.useWatchdog) {
        monitor->watchdogRunning = true;
        pthread_create(&monitor->watchdogThread, NULL, watchdog_thread, monitor);
    }

    monitor->state = MONITOR_STATE_RUNNING;
    
    pthread_mutex_unlock(&monitor->mutex);
}

void rtmp_stability_stop(RTMPStabilityMonitor *monitor) {
    if (!monitor || monitor->state == MONITOR_STATE_STOPPED) return;

    pthread_mutex_lock(&monitor->mutex);

    // Stop watchdog thread
    if (monitor->watchdogRunning) {
        monitor->watchdogRunning = false;
        pthread_join(monitor->watchdogThread, NULL);
    }

    monitor->state = MONITOR_STATE_STOPPED;
    
    pthread_mutex_unlock(&monitor->mutex);
}

void rtmp_stability_reset(RTMPStabilityMonitor *monitor) {
    if (!monitor) return;

    pthread_mutex_lock(&monitor->mutex);
    
    monitor->reconnectCount = 0;
    monitor->lastHeartbeat = rtmp_get_timestamp();
    monitor->lastResponse = monitor->lastHeartbeat;
    monitor->heartbeatPending = false;

    if (monitor->state == MONITOR_STATE_RECOVERING) {
        monitor->state = MONITOR_STATE_RUNNING;
    }
    
    pthread_mutex_unlock(&monitor->mutex);
}

bool rtmp_stability_is_stable(RTMPStabilityMonitor *monitor) {
    if (!monitor) return false;
    return monitor->state == MONITOR_STATE_RUNNING;
}

bool rtmp_stability_is_recovering(RTMPStabilityMonitor *monitor) {
    if (!monitor) return false;
    return monitor->state == MONITOR_STATE_RECOVERING;
}

uint32_t rtmp_stability_get_reconnect_count(RTMPStabilityMonitor *monitor) {
    if (!monitor) return 0;
    return monitor->reconnectCount;
}

bool rtmp_stability_try_recover(RTMPStabilityMonitor *monitor) {
    if (!monitor || monitor->state != MONITOR_STATE_FAILED) return false;

    pthread_mutex_lock(&monitor->mutex);
    bool result = attempt_reconnect(monitor);
    pthread_mutex_unlock(&monitor->mutex);

    return result;
}

void rtmp_stability_force_reconnect(RTMPStabilityMonitor *monitor) {
    if (!monitor) return;

    pthread_mutex_lock(&monitor->mutex);
    enter_recovery_mode(monitor, RTMP_RECOVERY_MODE_RECONNECT);
    pthread_mutex_unlock(&monitor->mutex);
}

void rtmp_stability_set_callback(RTMPStabilityMonitor *monitor, RTMPStabilityCallback callback, void *userData) {
    if (!monitor) return;

    pthread_mutex_lock(&monitor->mutex);
    monitor->callback = callback;
    monitor->userData = userData;
    pthread_mutex_unlock(&monitor->mutex);
}

static void *watchdog_thread(void *arg) {
    RTMPStabilityMonitor *monitor = (RTMPStabilityMonitor *)arg;
    uint32_t checkInterval = 1000; // Check every second

    while (monitor->watchdogRunning) {
        pthread_mutex_lock(&monitor->mutex);

        if (monitor->state == MONITOR_STATE_RUNNING) {
            uint32_t now = rtmp_get_timestamp();

            // Send heartbeat if needed
            if (monitor->config.useHeartbeat && 
                !monitor->heartbeatPending && 
                now - monitor->lastHeartbeat >= monitor->config.heartbeatInterval) {
                send_heartbeat(monitor);
            }

            // Check connection status
            check_connection_status(monitor);
        }

        pthread_mutex_unlock(&monitor->mutex);
        rtmp_sleep_ms(checkInterval);
    }

    return NULL;
}

static bool attempt_reconnect(RTMPStabilityMonitor *monitor) {
    if (monitor->reconnectCount >= monitor->config.maxReconnectAttempts) {
        monitor->state = MONITOR_STATE_FAILED;
        return false;
    }

    // Close existing connection
    rtmp_disconnect(monitor->rtmp);
    rtmp_sleep_ms(monitor->config.reconnectDelay);

    // Try to reconnect
    if (rtmp_connect(monitor->rtmp, monitor->rtmp->host, monitor->rtmp->port)) {
        monitor->reconnectCount++;
        monitor->state = MONITOR_STATE_RUNNING;
        return true;
    }

    monitor->state = MONITOR_STATE_FAILED;
    return false;
}

static void send_heartbeat(RTMPStabilityMonitor *monitor) {
    // Send ping
    RTMPPacket packet = {
        .type = RTMP_MSG_USER_CONTROL,
        .size = 6,
        .data = (uint8_t *)"\x06\x00\x00\x00\x00\x00",
        .timestamp = rtmp_get_timestamp()
    };

    if (rtmp_send_packet(monitor->rtmp, &packet)) {
        monitor->lastHeartbeat = packet.timestamp;
        monitor->heartbeatPending = true;
    }
}

static void check_connection_status(RTMPStabilityMonitor *monitor) {
    uint32_t now = rtmp_get_timestamp();

    if (monitor->heartbeatPending && 
        now - monitor->lastHeartbeat >= monitor->config.watchdogTimeout) {
        // Connection appears to be dead
        enter_recovery_mode(monitor, monitor->config.recoveryMode);
    }
}

static void enter_recovery_mode(RTMPStabilityMonitor *monitor, RTMPRecoveryMode mode) {
    if (monitor->state != MONITOR_STATE_RUNNING) return;

    monitor->state = MONITOR_STATE_RECOVERING;

    if (monitor->callback) {
        monitor->callback(monitor, mode, monitor->userData);
    }

    switch (mode) {
        case RTMP_RECOVERY_MODE_RECONNECT:
            if (monitor->config.autoReconnect) {
                attempt_reconnect(monitor);
            }
            break;

        case RTMP_RECOVERY_MODE_RESET:
            rtmp_stability_reset(monitor);
            break;

        case RTMP_RECOVERY_MODE_FALLBACK:
            monitor->state = MONITOR_STATE_FAILED;
            break;

        default:
            break;
    }
}

static void exit_recovery_mode(RTMPStabilityMonitor *monitor, bool success) {
    if (monitor->state != MONITOR_STATE_RECOVERING) return;

    monitor->state = success ? MONITOR_STATE_RUNNING : MONITOR_STATE_FAILED;

    if (monitor->callback) {
        monitor->callback(monitor, RTMP_RECOVERY_MODE_NONE, monitor->userData);
    }
}