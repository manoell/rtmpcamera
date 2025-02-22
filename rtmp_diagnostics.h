#ifndef RTMP_DIAGNOSTICS_H
#define RTMP_DIAGNOSTICS_H

#include <stdbool.h>
#include <stdint.h>

// Níveis de log
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level_t;

// Eventos de diagnóstico
typedef enum {
    EVENT_STREAM_START,
    EVENT_STREAM_STOP,
    EVENT_QUALITY_CHANGE,
    EVENT_FAILOVER,
    EVENT_ERROR
} diagnostic_event_t;

// Configuração de diagnóstico
typedef struct {
    log_level_t min_log_level;
    bool qos_enabled;
    bool failover_enabled;
    uint32_t buffer_size;
} diagnostic_config_t;

// Estatísticas de diagnóstico
typedef struct {
    uint32_t total_logs;
    uint32_t error_count;
    uint64_t bytes_transmitted;
    uint32_t peak_bandwidth;
} diagnostic_stats_t;

// Estatísticas de rede
typedef struct {
    float bandwidth_mbps;
    uint32_t latency_ms;
    float packet_loss;
} network_stats_t;

// Funções principais
bool rtmp_diagnostics_init(const char *log_path, diagnostic_config_t config);
void rtmp_diagnostics_log(log_level_t level, const char *format, ...);
void rtmp_diagnostics_update_stream_stats(const stream_stats_t *stats);
void rtmp_diagnostics_update_network_stats(const network_stats_t *stats);
void rtmp_diagnostics_record_event(diagnostic_event_t event, const char *details);
diagnostic_stats_t rtmp_diagnostics_get_stats(void);
void rtmp_diagnostics_shutdown(void);

#endif // RTMP_DIAGNOSTICS_H