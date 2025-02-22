#ifndef RTMP_FAILOVER_H
#define RTMP_FAILOVER_H

#include <stdbool.h>
#include "rtmp_stream.h"

// Tipos de eventos de failover
typedef enum {
    FAILOVER_SWITCHED_TO_BACKUP,
    FAILOVER_RECOVERED_TO_PRIMARY,
    FAILOVER_ERROR
} failover_event_t;

// Estatísticas de failover
typedef struct {
    uint32_t failover_count;
    uint32_t recovery_count;
    uint32_t health_issues;
} failover_stats_t;

// Callback para eventos de failover
typedef void (*failover_callback_t)(failover_event_t event, void *ctx);

// Handle opaco para o controlador de failover
typedef struct rtmp_failover rtmp_failover_t;

// Funções principais
rtmp_failover_t *rtmp_failover_create(const char *primary_url, 
                                     const char *backup_url);
int rtmp_failover_start(rtmp_failover_t *failover);
void rtmp_failover_stop(rtmp_failover_t *failover);
rtmp_stream_t *rtmp_failover_get_current_stream(rtmp_failover_t *failover);
void rtmp_failover_set_callback(rtmp_failover_t *failover,
                               failover_callback_t callback,
                               void *ctx);
failover_stats_t rtmp_failover_get_stats(rtmp_failover_t *failover);
void rtmp_failover_destroy(rtmp_failover_t *failover);

#endif // RTMP_FAILOVER_H