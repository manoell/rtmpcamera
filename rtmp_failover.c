#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "rtmp_failover.h"
#include "rtmp_core.h"
#include "rtmp_utils.h"

#define MAX_RETRY_ATTEMPTS 5
#define RETRY_DELAY_MS 1000
#define HEALTH_CHECK_INTERVAL_MS 2000
#define CONNECTION_TIMEOUT_MS 5000

struct rtmp_failover {
    rtmp_stream_t *primary_stream;
    rtmp_stream_t *backup_stream;
    
    char *primary_url;
    char *backup_url;
    
    pthread_t monitor_thread;
    pthread_mutex_t lock;
    
    bool is_running;
    bool using_backup;
    int retry_count;
    
    uint32_t last_health_check;
    failover_stats_t stats;
    
    failover_callback_t callback;
    void *callback_ctx;
};

static void *monitor_thread(void *arg) {
    rtmp_failover_t *failover = (rtmp_failover_t *)arg;
    
    while (failover->is_running) {
        uint32_t current_time = get_current_timestamp();
        
        if (current_time - failover->last_health_check >= HEALTH_CHECK_INTERVAL_MS) {
            pthread_mutex_lock(&failover->lock);
            
            rtmp_stream_t *current_stream = failover->using_backup ? 
                                          failover->backup_stream : 
                                          failover->primary_stream;
            
            // Verifica saúde do stream atual
            stream_stats_t stream_stats = rtmp_stream_get_stats(current_stream);
            bool is_healthy = stream_stats.buffer_usage < 0.9 && 
                            stream_stats.current_fps > 25.0;
            
            if (!is_healthy) {
                failover->stats.health_issues++;
                
                if (!failover->using_backup) {
                    // Tenta mudar para backup
                    if (rtmp_stream_is_connected(failover->backup_stream)) {
                        failover->using_backup = true;
                        failover->stats.failover_count++;
                        
                        if (failover->callback) {
                            failover->callback(FAILOVER_SWITCHED_TO_BACKUP, 
                                            failover->callback_ctx);
                        }
                    }
                } else {
                    // Tenta reconectar ao primário
                    if (failover->retry_count < MAX_RETRY_ATTEMPTS) {
                        if (rtmp_stream_connect(failover->primary_stream, 
                                              failover->primary_url) == 0) {
                            failover->using_backup = false;
                            failover->retry_count = 0;
                            failover->stats.recovery_count++;
                            
                            if (failover->callback) {
                                failover->callback(FAILOVER_RECOVERED_TO_PRIMARY, 
                                                failover->callback_ctx);
                            }
                        } else {
                            failover->retry_count++;
                        }
                    }
                }
            } else {
                failover->retry_count = 0;
            }
            
            failover->last_health_check = current_time;
            pthread_mutex_unlock(&failover->lock);
        }
        
        // Economia de CPU
        usleep(100000); // 100ms
    }
    
    return NULL;
}

rtmp_failover_t *rtmp_failover_create(const char *primary_url, 
                                     const char *backup_url) {
    rtmp_failover_t *failover = calloc(1, sizeof(rtmp_failover_t));
    if (!failover) return NULL;
    
    failover->primary_url = strdup(primary_url);
    failover->backup_url = strdup(backup_url);
    
    failover->primary_stream = rtmp_stream_create(NULL);
    failover->backup_stream = rtmp_stream_create(NULL);
    
    pthread_mutex_init(&failover->lock, NULL);
    
    return failover;
}

int rtmp_failover_start(rtmp_failover_t *failover) {
    if (!failover) return -1;
    
    // Conecta ao stream primário
    if (rtmp_stream_connect(failover->primary_stream, failover->primary_url) != 0) {
        // Se falhar, tenta o backup
        if (rtmp_stream_connect(failover->backup_stream, failover->backup_url) != 0) {
            return -1;
        }
        failover->using_backup = true;
        failover->stats.failover_count++;
    }
    
    // Inicia monitoramento
    failover->is_running = true;
    if (pthread_create(&failover->monitor_thread, NULL, monitor_thread, failover) != 0) {
        rtmp_stream_disconnect(failover->primary_stream);
        rtmp_stream_disconnect(failover->backup_stream);
        return -1;
    }
    
    return 0;
}

void rtmp_failover_stop(rtmp_failover_t *failover) {
    if (!failover) return;
    
    failover->is_running = false;
    pthread_join(failover->monitor_thread, NULL);
    
    rtmp_stream_disconnect(failover->primary_stream);
    rtmp_stream_disconnect(failover->backup_stream);
}

rtmp_stream_t *rtmp_failover_get_current_stream(rtmp_failover_t *failover) {
    if (!failover) return NULL;
    
    return failover->using_backup ? failover->backup_stream : failover->primary_stream;
}

void rtmp_failover_set_callback(rtmp_failover_t *failover, 
                               failover_callback_t callback, 
                               void *ctx) {
    if (!failover) return;
    
    pthread_mutex_lock(&failover->lock);
    failover->callback = callback;
    failover->callback_ctx = ctx;
    pthread_mutex_unlock(&failover->lock);
}

failover_stats_t rtmp_failover_get_stats(rtmp_failover_t *failover) {
    failover_stats_t stats = {0};
    if (!failover) return stats;
    
    pthread_mutex_lock(&failover->lock);
    stats = failover->stats;
    pthread_mutex_unlock(&failover->lock);
    
    return stats;
}

void rtmp_failover_destroy(rtmp_failover_t *failover) {
    if (!failover) return;
    
    rtmp_failover_stop(failover);
    
    rtmp_stream_destroy(failover->primary_stream);
    rtmp_stream_destroy(failover->backup_stream);
    
    free(failover->primary_url);
    free(failover->backup_url);
    
    pthread_mutex_destroy(&failover->lock);
    
    free(failover);
}