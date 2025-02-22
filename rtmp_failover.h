#ifndef RTMP_FAILOVER_H
#define RTMP_FAILOVER_H

#include <stdint.h>
#include <stdbool.h>

// Error codes
#define RTMP_FAILOVER_SUCCESS 0
#define RTMP_FAILOVER_ERROR_INVALID_PARAM -1
#define RTMP_FAILOVER_ERROR_MAX_SERVERS -2
#define RTMP_FAILOVER_ERROR_THREAD -3
#define RTMP_FAILOVER_ERROR_ALREADY_RUNNING -4

// Forward declaration
typedef struct rtmp_failover_context rtmp_failover_context_t;

// Callback function types
typedef bool (*rtmp_failover_check_callback)(const char *url, void *user_data);
typedef int (*rtmp_failover_connect_callback)(const char *url, void *user_data);
typedef void (*rtmp_failover_switch_callback)(const char *url, void *user_data);
typedef void (*rtmp_failover_fail_callback)(const char *url, void *user_data);

// Callback structure
typedef struct {
    rtmp_failover_check_callback check_server;
    rtmp_failover_connect_callback connect_server;
    rtmp_failover_switch_callback server_switched;
    rtmp_failover_fail_callback server_failed;
} rtmp_failover_callbacks_t;

// Configuration structure
typedef struct {
    uint32_t max_retries;
    uint32_t retry_delay;
    uint32_t check_interval;
    bool auto_reconnect;
} rtmp_failover_config_t;

// Statistics structure
typedef struct {
    uint64_t start_time;
    uint64_t uptime;
    uint32_t switches;
    uint32_t failures;
    uint32_t total_downtime;
} rtmp_failover_stats_t;

// Core functions
rtmp_failover_context_t* rtmp_failover_create(const rtmp_failover_config_t *config);
void rtmp_failover_destroy(rtmp_failover_context_t *ctx);
int rtmp_failover_start(rtmp_failover_context_t *ctx);
int rtmp_failover_stop(rtmp_failover_context_t *ctx);

// Server management
int rtmp_failover_add_server(rtmp_failover_context_t *ctx,
                            const char *url,
                            int priority);

const char* rtmp_failover_get_current_server(rtmp_failover_context_t *ctx);

// Callback management
void rtmp_failover_set_callbacks(rtmp_failover_context_t *ctx,
                               const rtmp_failover_callbacks_t *callbacks,
                               void *user_data);

// Statistics
const rtmp_failover_stats_t* rtmp_failover_get_stats(rtmp_failover_context_t *ctx);

#endif // RTMP_FAILOVER_H