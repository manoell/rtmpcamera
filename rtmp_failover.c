#include "rtmp_failover.h"
#include "rtmp_utils.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define DEFAULT_SWITCH_TIMEOUT 5000    // 5 seconds
#define DEFAULT_HEALTH_INTERVAL 10000  // 10 seconds
#define DEFAULT_MAX_ATTEMPTS 3
#define NETWORK_CHECK_INTERVAL 1000    // 1 second

struct RTMPFailoverHandler {
    RTMPContext *rtmp;
    RTMPFailoverConfig config;
    RTMPFailoverStatus status;
    RTMPFailoverCallback callback;
    void *userData;
    pthread_t healthThread;
    pthread_mutex_t mutex;
    bool threadRunning;
};

// Private helper functions
static void *health_check_thread(void *arg);
static bool switch_server(RTMPFailoverHandler *handler);
static bool switch_network(RTMPFailoverHandler *handler);
static bool switch_to_local(RTMPFailoverHandler *handler);
static void perform_health_check(RTMPFailoverHandler *handler);
static bool reconnect_to_server(RTMPFailoverHandler *handler, const char *server);
static bool is_network_available(const char *interface);
static bool start_local_recording(RTMPFailoverHandler *handler);
static void stop_local_recording(RTMPFailoverHandler *handler);

RTMPFailoverHandler *rtmp_failover_create(RTMPContext *rtmp) {
    if (!rtmp) return NULL;

    RTMPFailoverHandler *handler = (RTMPFailoverHandler *)calloc(1, sizeof(RTMPFailoverHandler));
    if (!handler) return NULL;

    handler->rtmp = rtmp;
    
    // Initialize mutex
    pthread_mutex_init(&handler->mutex, NULL);
    
    // Set default config
    handler->config.enableServerFailover = true;
    handler->config.enableNetworkFailover = true;
    handler->config.enableLocalFailover = true;
    handler->config.maxSwitchAttempts = DEFAULT_MAX_ATTEMPTS;
    handler->config.switchTimeout = DEFAULT_SWITCH_TIMEOUT;
    handler->config.healthCheckInterval = DEFAULT_HEALTH_INTERVAL;
    
    // Initialize status
    handler->status.state = RTMP_FAILOVER_STATE_IDLE;
    handler->status.currentType = RTMP_FAILOVER_NONE;
    handler->status.isHealthy = true;

    return handler;
}

void rtmp_failover_destroy(RTMPFailoverHandler *handler) {
    if (!handler) return;

    rtmp_failover_stop(handler);
    pthread_mutex_destroy(&handler->mutex);
    free(handler);
}

void rtmp_failover_set_config(RTMPFailoverHandler *handler, const RTMPFailoverConfig *config) {
    if (!handler || !config) return;

    pthread_mutex_lock(&handler->mutex);
    memcpy(&handler->config, config, sizeof(RTMPFailoverConfig));
    pthread_mutex_unlock(&handler->mutex);
}

const RTMPFailoverConfig *rtmp_failover_get_config(RTMPFailoverHandler *handler) {
    if (!handler) return NULL;
    return &handler->config;
}

void rtmp_failover_start(RTMPFailoverHandler *handler) {
    if (!handler) return;

    pthread_mutex_lock(&handler->mutex);
    
    if (handler->status.state == RTMP_FAILOVER_STATE_IDLE) {
        handler->status.state = RTMP_FAILOVER_STATE_ACTIVE;
        handler->status.switchAttempts = 0;
        handler->status.lastSwitchTime = rtmp_get_timestamp();
        handler->status.healthCheckTime = handler->status.lastSwitchTime;
        
        // Start health check thread
        handler->threadRunning = true;
        pthread_create(&handler->healthThread, NULL, health_check_thread, handler);
    }
    
    pthread_mutex_unlock(&handler->mutex);
}

void rtmp_failover_stop(RTMPFailoverHandler *handler) {
    if (!handler) return;

    pthread_mutex_lock(&handler->mutex);
    
    if (handler->status.state != RTMP_FAILOVER_STATE_IDLE) {
        // Stop health check thread
        handler->threadRunning = false;
        pthread_join(handler->healthThread, NULL);
        
        // Stop local recording if active
        if (handler->status.currentType == RTMP_FAILOVER_LOCAL) {
            stop_local_recording(handler);
        }
        
        handler->status.state = RTMP_FAILOVER_STATE_IDLE;
        handler->status.currentType = RTMP_FAILOVER_NONE;
    }
    
    pthread_mutex_unlock(&handler->mutex);
}

void rtmp_failover_reset(RTMPFailoverHandler *handler) {
    if (!handler) return;

    pthread_mutex_lock(&handler->mutex);
    
    handler->status.switchAttempts = 0;
    handler->status.lastSwitchTime = rtmp_get_timestamp();
    handler->status.healthCheckTime = handler->status.lastSwitchTime;
    handler->status.isHealthy = true;
    
    if (handler->status.state == RTMP_FAILOVER_STATE_FAILED) {
        handler->status.state = RTMP_FAILOVER_STATE_ACTIVE;
    }
    
    pthread_mutex_unlock(&handler->mutex);
}

bool rtmp_failover_trigger(RTMPFailoverHandler *handler, RTMPFailoverType type) {
    if (!handler) return false;

    pthread_mutex_lock(&handler->mutex);
    
    bool success = false;
    
    if (handler->status.state == RTMP_FAILOVER_STATE_ACTIVE &&
        handler->status.switchAttempts < handler->config.maxSwitchAttempts) {
        
        handler->status.state = RTMP_FAILOVER_STATE_SWITCHING;
        handler->status.currentType = type;
        handler->status.switchAttempts++;
        
        switch (type) {
            case RTMP_FAILOVER_SERVER:
                if (handler->config.enableServerFailover) {
                    success = switch_server(handler);
                }
                break;
                
            case RTMP_FAILOVER_NETWORK:
                if (handler->config.enableNetworkFailover) {
                    success = switch_network(handler);
                }
                break;
                
            case RTMP_FAILOVER_LOCAL:
                if (handler->config.enableLocalFailover) {
                    success = switch_to_local(handler);
                }
                break;
                
            default:
                break;
        }
        
        if (success) {
            handler->status.state = RTMP_FAILOVER_STATE_ACTIVE;
            handler->status.lastSwitchTime = rtmp_get_timestamp();
        } else {
            handler->status.state = RTMP_FAILOVER_STATE_FAILED;
        }
        
        if (handler->callback) {
            handler->callback(handler, type, success, handler->userData);
        }
    }
    
    pthread_mutex_unlock(&handler->mutex);
    
    return success;
}

RTMPFailoverStatus *rtmp_failover_get_status(RTMPFailoverHandler *handler) {
    if (!handler) return NULL;
    return &handler->status;
}

bool rtmp_failover_is_active(RTMPFailoverHandler *handler) {
    if (!handler) return false;
    return handler->status.state == RTMP_FAILOVER_STATE_ACTIVE;
}

bool rtmp_failover_is_healthy(RTMPFailoverHandler *handler) {
    if (!handler) return false;
    return handler->status.isHealthy;
}

void rtmp_failover_check_health(RTMPFailoverHandler *handler) {
    if (!handler) return;
    perform_health_check(handler);
}

void rtmp_failover_set_healthy(RTMPFailoverHandler *handler, bool healthy) {
    if (!handler) return;

    pthread_mutex_lock(&handler->mutex);
    handler->status.isHealthy = healthy;
    pthread_mutex_unlock(&handler->mutex);
}

void rtmp_failover_set_callback(RTMPFailoverHandler *handler,
                               RTMPFailoverCallback callback,
                               void *userData) {
    if (!handler) return;

    pthread_mutex_lock(&handler->mutex);
    handler->callback = callback;
    handler->userData = userData;
    pthread_mutex_unlock(&handler->mutex);
}

static void *health_check_thread(void *arg) {
    RTMPFailoverHandler *handler = (RTMPFailoverHandler *)arg;
    
    while (handler->threadRunning) {
        pthread_mutex_lock(&handler->mutex);
        
        if (handler->status.state == RTMP_FAILOVER_STATE_ACTIVE) {
            uint32_t now = rtmp_get_timestamp();
            
            if (now - handler->status.healthCheckTime >= handler->config.healthCheckInterval) {
                perform_health_check(handler);
                handler->status.healthCheckTime = now;
            }
        }
        
        pthread_mutex_unlock(&handler->mutex);
        rtmp_sleep_ms(NETWORK_CHECK_INTERVAL);
    }
    
    return NULL;
}

static bool switch_server(RTMPFailoverHandler *handler) {
    // Try each backup server in order
    for (uint32_t i = 0; i < handler->config.numBackupServers; i++) {
        if (reconnect_to_server(handler, handler->config.backupServers[i])) {
            strncpy(handler->status.currentServer, 
                   handler->config.backupServers[i],
                   sizeof(handler->status.currentServer) - 1);
            return true;
        }
    }
    
    return false;
}

static bool switch_network(RTMPFailoverHandler *handler) {
    // Get list of available network interfaces
    char interfaces[4][64] = {"en0", "en1", "pdp_ip0", "pdp_ip1"}; // Example interfaces
    
    for (int i = 0; i < 4; i++) {
        if (strcmp(interfaces[i], handler->status.currentNetwork) != 0 &&
            is_network_available(interfaces[i])) {
            
            // Try to reconnect using this interface
            if (reconnect_to_server(handler, handler->status.currentServer)) {
                strncpy(handler->status.currentNetwork,
                       interfaces[i],
                       sizeof(handler->status.currentNetwork) - 1);
                return true;
            }
        }
    }
    
    return false;
}

static bool switch_to_local(RTMPFailoverHandler *handler) {
    // Stop current streaming
    rtmp_disconnect(handler->rtmp);
    
    // Start local recording
    return start_local_recording(handler);
}

static void perform_health_check(RTMPFailoverHandler *handler) {
    bool healthy = true;
    
    // Check connection status
    if (!rtmp_is_connected(handler->rtmp)) {
        healthy = false;
    }
    
    // Check network status
    if (!is_network_available(handler->status.currentNetwork)) {
        healthy = false;
    }

    // Check server response
    RTMPPacket ping = {
        .type = RTMP_MSG_USER_CONTROL,
        .size = 6,
        .data = (uint8_t *)"\x06\x00\x00\x00\x00\x00",
        .timestamp = rtmp_get_timestamp()
    };

    if (!rtmp_send_packet(handler->rtmp, &ping)) {
        healthy = false;
    }

    // Update health status
    handler->status.isHealthy = healthy;

    // Trigger failover if unhealthy
    if (!healthy) {
        if (handler->config.enableServerFailover) {
            rtmp_failover_trigger(handler, RTMP_FAILOVER_SERVER);
        } else if (handler->config.enableNetworkFailover) {
            rtmp_failover_trigger(handler, RTMP_FAILOVER_NETWORK);
        } else if (handler->config.enableLocalFailover) {
            rtmp_failover_trigger(handler, RTMP_FAILOVER_LOCAL);
        }
    }
}

static bool reconnect_to_server(RTMPFailoverHandler *handler, const char *server) {
    // Close existing connection
    rtmp_disconnect(handler->rtmp);
    rtmp_sleep_ms(1000); // Wait 1 second before reconnecting

    // Parse server URL
    char host[256];
    int port = RTMP_DEFAULT_PORT;
    char app[128];
    
    if (sscanf(server, "rtmp://%255[^:]:%d/%127s", host, &port, app) < 3) {
        if (sscanf(server, "rtmp://%255[^/]/%127s", host, app) < 2) {
            return false;
        }
    }

    // Try to connect
    uint32_t startTime = rtmp_get_timestamp();
    bool connected = false;

    while (!connected && (rtmp_get_timestamp() - startTime < handler->config.switchTimeout)) {
        if (rtmp_connect(handler->rtmp, host, port)) {
            connected = true;
            break;
        }
        rtmp_sleep_ms(500);
    }

    return connected;
}

static bool is_network_available(const char *interface) {
    // Check if network interface is up and has valid IP
    // This is a simplified check - in practice you'd want to use platform specific APIs
    
    if (!interface) return false;

    // For iOS you'd use SCNetworkReachability APIs here
    // For now we just assume en0 (WiFi) and pdp_ip0 (Cellular) are valid
    return (strcmp(interface, "en0") == 0 || 
            strcmp(interface, "pdp_ip0") == 0);
}

static bool start_local_recording(RTMPFailoverHandler *handler) {
    if (!handler->config.localRecordingPath[0]) return false;

    // Here you'd implement local recording logic
    // This could involve writing to a file or using AVFoundation to record
    
    // For this example we just pretend it worked
    return true;
}

static void stop_local_recording(RTMPFailoverHandler *handler) {
    // Here you'd implement logic to stop local recording
    // This could involve closing files or stopping AVFoundation recording
}