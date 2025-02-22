#include "rtmp_failover.h"
#include "rtmp_utils.h"
#include "rtmp_diagnostics.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#define RTMP_FAILOVER_MAX_RETRIES 3
#define RTMP_FAILOVER_RETRY_DELAY 5000 // 5 seconds
#define RTMP_FAILOVER_CHECK_INTERVAL 1000 // 1 second
#define RTMP_FAILOVER_MAX_SERVERS 10

typedef struct {
    char *url;
    int priority;
    bool active;
    uint64_t last_attempt;
    uint32_t fail_count;
} rtmp_server_info_t;

struct rtmp_failover_context {
    rtmp_server_info_t servers[RTMP_FAILOVER_MAX_SERVERS];
    size_t server_count;
    int current_server;
    pthread_mutex_t mutex;
    pthread_t monitor_thread;
    bool running;
    rtmp_failover_config_t config;
    rtmp_failover_callbacks_t callbacks;
    void *user_data;
    rtmp_failover_stats_t stats;
};

// Create failover context
rtmp_failover_context_t* rtmp_failover_create(const rtmp_failover_config_t *config) {
    rtmp_failover_context_t *ctx = rtmp_utils_malloc(sizeof(rtmp_failover_context_t));
    if (!ctx) {
        rtmp_log_error("Failed to allocate failover context");
        return NULL;
    }
    
    if (pthread_mutex_init(&ctx->mutex, NULL) != 0) {
        rtmp_utils_free(ctx);
        rtmp_log_error("Failed to initialize failover mutex");
        return NULL;
    }
    
    ctx->server_count = 0;
    ctx->current_server = -1;
    ctx->running = false;
    
    // Copy configuration
    if (config) {
        memcpy(&ctx->config, config, sizeof(rtmp_failover_config_t));
    } else {
        // Default configuration
        ctx->config.max_retries = RTMP_FAILOVER_MAX_RETRIES;
        ctx->config.retry_delay = RTMP_FAILOVER_RETRY_DELAY;
        ctx->config.check_interval = RTMP_FAILOVER_CHECK_INTERVAL;
        ctx->config.auto_reconnect = true;
    }
    
    memset(&ctx->stats, 0, sizeof(rtmp_failover_stats_t));
    ctx->stats.start_time = rtmp_utils_get_time_ms();
    
    return ctx;
}

// Add server to failover list
int rtmp_failover_add_server(rtmp_failover_context_t *ctx, const char *url, int priority) {
    if (!ctx || !url) return RTMP_FAILOVER_ERROR_INVALID_PARAM;
    
    pthread_mutex_lock(&ctx->mutex);
    
    if (ctx->server_count >= RTMP_FAILOVER_MAX_SERVERS) {
        pthread_mutex_unlock(&ctx->mutex);
        rtmp_log_error("Maximum number of failover servers reached");
        return RTMP_FAILOVER_ERROR_MAX_SERVERS;
    }
    
    // Find insert position based on priority
    size_t pos = 0;
    while (pos < ctx->server_count && ctx->servers[pos].priority <= priority) {
        pos++;
    }
    
    // Shift existing servers
    if (pos < ctx->server_count) {
        memmove(&ctx->servers[pos + 1], &ctx->servers[pos],
                (ctx->server_count - pos) * sizeof(rtmp_server_info_t));
    }
    
    // Add new server
    ctx->servers[pos].url = rtmp_utils_strdup(url);
    ctx->servers[pos].priority = priority;
    ctx->servers[pos].active = false;
    ctx->servers[pos].last_attempt = 0;
    ctx->servers[pos].fail_count = 0;
    
    ctx->server_count++;
    
    pthread_mutex_unlock(&ctx->mutex);
    
    rtmp_log_info("Added failover server: %s (priority: %d)", url, priority);
    return RTMP_FAILOVER_SUCCESS;
}

// Monitor thread function
static void* rtmp_failover_monitor(void *arg) {
    rtmp_failover_context_t *ctx = (rtmp_failover_context_t*)arg;
    uint64_t last_check = rtmp_utils_get_time_ms();
    
    while (ctx->running) {
        uint64_t current_time = rtmp_utils_get_time_ms();
        
        // Check servers periodically
        if (current_time - last_check >= ctx->config.check_interval) {
            pthread_mutex_lock(&ctx->mutex);
            
            // Check current server
            if (ctx->current_server >= 0) {
                rtmp_server_info_t *server = &ctx->servers[ctx->current_server];
                
                // Check if server is healthy
                if (ctx->callbacks.check_server) {
                    if (!ctx->callbacks.check_server(server->url, ctx->user_data)) {
                        server->fail_count++;
                        ctx->stats.failures++;
                        
                        rtmp_log_warning("Server %s health check failed (%d/%d)",
                                      server->url, server->fail_count,
                                      ctx->config.max_retries);
                        
                        // Switch to next server if max retries exceeded
                        if (server->fail_count >= ctx->config.max_retries) {
                            server->active = false;
                            ctx->current_server = -1;
                            
                            if (ctx->callbacks.server_failed) {
                                ctx->callbacks.server_failed(server->url, ctx->user_data);
                            }
                        }
                    } else {
                        server->fail_count = 0;
                    }
                }
            }
            
            // Try to connect to a new server if needed
            if (ctx->current_server < 0 && ctx->config.auto_reconnect) {
                for (size_t i = 0; i < ctx->server_count; i++) {
                    rtmp_server_info_t *server = &ctx->servers[i];
                    
                    // Skip recently failed servers
                    if (current_time - server->last_attempt < ctx->config.retry_delay) {
                        continue;
                    }
                    
                    // Try to connect
                    server->last_attempt = current_time;
                    
                    if (ctx->callbacks.connect_server &&
                        ctx->callbacks.connect_server(server->url, ctx->user_data) == RTMP_FAILOVER_SUCCESS) {
                        server->active = true;
                        server->fail_count = 0;
                        ctx->current_server = i;
                        ctx->stats.switches++;
                        
                        rtmp_log_info("Switched to failover server: %s", server->url);
                        
                        if (ctx->callbacks.server_switched) {
                            ctx->callbacks.server_switched(server->url, ctx->user_data);
                        }
                        break;
                    }
                }
            }
            
            pthread_mutex_unlock(&ctx->mutex);
            last_check = current_time;
        }
        
        rtmp_utils_sleep_ms(100); // Sleep to prevent CPU overload
    }
    
    return NULL;
}

// Start failover system
int rtmp_failover_start(rtmp_failover_context_t *ctx) {
    if (!ctx) return RTMP_FAILOVER_ERROR_INVALID_PARAM;
    
    pthread_mutex_lock(&ctx->mutex);
    
    if (ctx->running) {
        pthread_mutex_unlock(&ctx->mutex);
        return RTMP_FAILOVER_ERROR_ALREADY_RUNNING;
    }
    
    ctx->running = true;
    
    // Start monitor thread
    if (pthread_create(&ctx->monitor_thread, NULL, rtmp_failover_monitor, ctx) != 0) {
        ctx->running = false;
        pthread_mutex_unlock(&ctx->mutex);
        rtmp_log_error("Failed to create failover monitor thread");
        return RTMP_FAILOVER_ERROR_THREAD;
    }
    
    pthread_mutex_unlock(&ctx->mutex);
    
    rtmp_log_info("Failover system started");
    return RTMP_FAILOVER_SUCCESS;
}

// Stop failover system
int rtmp_failover_stop(rtmp_failover_context_t *ctx) {
    if (!ctx) return RTMP_FAILOVER_ERROR_INVALID_PARAM;
    
    pthread_mutex_lock(&ctx->mutex);
    
    if (!ctx->running) {
        pthread_mutex_unlock(&ctx->mutex);
        return RTMP_FAILOVER_SUCCESS;
    }
    
    ctx->running = false;
    pthread_mutex_unlock(&ctx->mutex);
    
    // Wait for monitor thread to finish
    pthread_join(ctx->monitor_thread, NULL);
    
    rtmp_log_info("Failover system stopped");
    return RTMP_FAILOVER_SUCCESS;
}

// Set callbacks
void rtmp_failover_set_callbacks(rtmp_failover_context_t *ctx,
                               const rtmp_failover_callbacks_t *callbacks,
                               void *user_data) {
    if (!ctx || !callbacks) return;
    
    pthread_mutex_lock(&ctx->mutex);
    memcpy(&ctx->callbacks, callbacks, sizeof(rtmp_failover_callbacks_t));
    ctx->user_data = user_data;
    pthread_mutex_unlock(&ctx->mutex);
}

// Get current server
const char* rtmp_failover_get_current_server(rtmp_failover_context_t *ctx) {
    if (!ctx) return NULL;
    
    pthread_mutex_lock(&ctx->mutex);
    const char *url = ctx->current_server >= 0 ?
                     ctx->servers[ctx->current_server].url : NULL;
    pthread_mutex_unlock(&ctx->mutex);
    
    return url;
}

// Get statistics
const rtmp_failover_stats_t* rtmp_failover_get_stats(rtmp_failover_context_t *ctx) {
    if (!ctx) return NULL;
    
    pthread_mutex_lock(&ctx->mutex);
    
    // Update uptime
    ctx->stats.uptime = rtmp_utils_get_time_ms() - ctx->stats.start_time;
    
    const rtmp_failover_stats_t *stats = &ctx->stats;
    pthread_mutex_unlock(&ctx->mutex);
    
    return stats;
}

// Cleanup
void rtmp_failover_destroy(rtmp_failover_context_t *ctx) {
    if (!ctx) return;
    
    rtmp_failover_stop(ctx);
    
    pthread_mutex_lock(&ctx->mutex);
    
    // Free server URLs
    for (size_t i = 0; i < ctx->server_count; i++) {
        rtmp_utils_free(ctx->servers[i].url);
    }
    
    pthread_mutex_unlock(&ctx->mutex);
    pthread_mutex_destroy(&ctx->mutex);
    
    rtmp_utils_free(ctx);
}