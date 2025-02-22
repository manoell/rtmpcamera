#include "rtmp_stability.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/time.h>

#define MAX_RETRY_COUNT 5
#define RETRY_DELAY_MS 1000
#define HEARTBEAT_INTERVAL_MS 2000
#define CONNECTION_TIMEOUT_MS 5000

typedef struct {
    int running;
    int connected;
    int retry_count;
    pthread_t monitor_thread;
    uint64_t last_heartbeat;
    uint64_t connection_start;
    StabilityConfig config;
    StabilityCallback callback;
    void* user_data;
    
    struct {
        uint32_t disconnections;
        uint32_t reconnections;
        uint32_t failed_heartbeats;
        uint64_t total_uptime;
        uint64_t last_downtime;
    } metrics;
    
    pthread_mutex_t mutex;
} StabilityController;

static StabilityController* stability = NULL;

// Obtém timestamp atual em ms
static uint64_t get_timestamp_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

// Thread de monitoramento
static void* monitor_connection(void* arg) {
    StabilityController* ctrl = (StabilityController*)arg;
    uint64_t last_check = get_timestamp_ms();
    
    while (ctrl->running) {
        uint64_t now = get_timestamp_ms();
        int should_reconnect = 0;
        
        pthread_mutex_lock(&ctrl->mutex);
        
        // Verifica timeout de heartbeat
        if (ctrl->connected && (now - ctrl->last_heartbeat) > CONNECTION_TIMEOUT_MS) {
            ctrl->metrics.failed_heartbeats++;
            should_reconnect = 1;
        }
        
        // Verifica necessidade de reconexão
        if (should_reconnect) {
            ctrl->connected = 0;
            ctrl->metrics.disconnections++;
            ctrl->metrics.last_downtime = now;
            
            if (ctrl->callback) {
                ctrl->callback(STABILITY_EVENT_DISCONNECTED, ctrl->user_data);
            }
            
            // Tenta reconexão se configurado
            if (ctrl->config.auto_reconnect && ctrl->retry_count < MAX_RETRY_COUNT) {
                ctrl->retry_count++;
                
                if (ctrl->callback) {
                    ctrl->callback(STABILITY_EVENT_RECONNECTING, ctrl->user_data);
                }
                
                // Espera delay entre tentativas
                usleep(RETRY_DELAY_MS * 1000);
                
                // Tenta reconexão
                if (ctrl->callback) {
                    if (ctrl->callback(STABILITY_EVENT_RECONNECT_ATTEMPT, ctrl->user_data) == 0) {
                        ctrl->connected = 1;
                        ctrl->metrics.reconnections++;
                        ctrl->retry_count = 0;
                        ctrl->last_heartbeat = now;
                        ctrl->metrics.total_uptime += (now - ctrl->metrics.last_downtime);
                        
                        ctrl->callback(STABILITY_EVENT_RECONNECTED, ctrl->user_data);
                    }
                }
            }
        }
        
        pthread_mutex_unlock(&ctrl->mutex);
        
        // Espera próximo ciclo
        usleep(100 * 1000); // 100ms
    }
    
    return NULL;
}

int rtmp_stability_init(StabilityConfig* config, StabilityCallback cb, void* user_data) {
    if (stability) return -1;
    
    stability = calloc(1, sizeof(StabilityController));
    
    if (config) {
        memcpy(&stability->config, config, sizeof(StabilityConfig));
    } else {
        // Configurações padrão
        stability->config.auto_reconnect = 1;
        stability->config.heartbeat_interval = HEARTBEAT_INTERVAL_MS;
        stability->config.connection_timeout = CONNECTION_TIMEOUT_MS;
    }
    
    stability->callback = cb;
    stability->user_data = user_data;
    stability->running = 1;
    stability->connection_start = get_timestamp_ms();
    
    pthread_mutex_init(&stability->mutex, NULL);
    
    // Inicia thread de monitoramento
    pthread_create(&stability->monitor_thread, NULL, monitor_connection, stability);
    
    return 0;
}

void rtmp_stability_heartbeat(void) {
    if (!stability) return;
    
    pthread_mutex_lock(&stability->mutex);
    stability->last_heartbeat = get_timestamp_ms();
    pthread_mutex_unlock(&stability->mutex);
}

void rtmp_stability_connected(void) {
    if (!stability) return;
    
    pthread_mutex_lock(&stability->mutex);
    stability->connected = 1;
    stability->retry_count = 0;
    stability->last_heartbeat = get_timestamp_ms();
    pthread_mutex_unlock(&stability->mutex);
}

void rtmp_stability_disconnected(void) {
    if (!stability) return;
    
    pthread_mutex_lock(&stability->mutex);
    stability->connected = 0;
    stability->metrics.disconnections++;
    stability->metrics.last_downtime = get_timestamp_ms();
    pthread_mutex_unlock(&stability->mutex);
}

void rtmp_stability_get_stats(StabilityStats* stats) {
    if (!stability || !stats) return;
    
    pthread_mutex_lock(&stability->mutex);
    stats->disconnections = stability->metrics.disconnections;
    stats->reconnections = stability->metrics.reconnections;
    stats->failed_heartbeats = stability->metrics.failed_heartbeats;
    stats->total_uptime = stability->metrics.total_uptime;
    stats->current_retry = stability->retry_count;
    stats->is_connected = stability->connected;
    pthread_mutex_unlock(&stability->mutex);
}

void rtmp_stability_destroy(void) {
    if (!stability) return;
    
    stability->running = 0;
    pthread_join(stability->monitor_thread, NULL);
    pthread_mutex_destroy(&stability->mutex);
    
    free(stability);
    stability = NULL;
}