#ifndef RTMP_DIAGNOSTICS_H
#define RTMP_DIAGNOSTICS_H

#include <stdint.h>
#include <stdarg.h>

// Níveis de log
#define RTMP_LOG_DEBUG   0
#define RTMP_LOG_INFO    1
#define RTMP_LOG_WARNING 2
#define RTMP_LOG_ERROR   3
#define RTMP_LOG_FATAL   4

// Flags de diagnóstico
#define RTMP_DIAG_NETWORK    (1 << 0)  // Diagnóstico de rede
#define RTMP_DIAG_QUALITY    (1 << 1)  // Diagnóstico de qualidade
#define RTMP_DIAG_MEMORY     (1 << 2)  // Diagnóstico de memória
#define RTMP_DIAG_TIMING     (1 << 3)  // Diagnóstico de timing
#define RTMP_DIAG_ALL        0xFFFFFFFF

// Estrutura para estatísticas de diagnóstico
typedef struct {
    uint64_t total_bytes_sent;
    uint64_t total_frames_sent;
    uint64_t dropped_frames;
    uint64_t reconnect_count;
    uint64_t error_count;
    float average_latency;
    float peak_memory_usage;
    float current_cpu_usage;
    uint64_t start_time;
    uint64_t uptime;
} rtmp_diagnostic_stats_t;

// Callback para logs
typedef void (*rtmp_log_callback_t)(int level, const char *module, const char *message, void *user_data);

// Inicializa sistema de diagnóstico
int rtmp_diagnostic_init(void);

// Finaliza sistema de diagnóstico
void rtmp_diagnostic_shutdown(void);

// Registra callback para logs
void rtmp_diagnostic_set_callback(rtmp_log_callback_t callback, void *user_data);

// Define nível mínimo de log
void rtmp_diagnostic_set_level(int level);

// Define flags de diagnóstico ativos
void rtmp_diagnostic_set_flags(uint32_t flags);

// Funções de log
void rtmp_diagnostic_log(int level, const char *module, const char *format, va_list args);
void rtmp_log_debug(const char *format, ...);
void rtmp_log_info(const char *format, ...);
void rtmp_log_warning(const char *format, ...);
void rtmp_log_error(const char *format, ...);
void rtmp_log_fatal(const char *format, ...);

// Obtém estatísticas de diagnóstico
int rtmp_diagnostic_get_stats(rtmp_diagnostic_stats_t *stats);

// Reseta estatísticas de diagnóstico
void rtmp_diagnostic_reset_stats(void);

// Marca evento de diagnóstico
void rtmp_diagnostic_mark_event(const char *event_name);

// Inicia medição de tempo
uint64_t rtmp_diagnostic_start_timing(const char *operation);

// Finaliza medição de tempo
void rtmp_diagnostic_end_timing(uint64_t timing_id);

#endif // RTMP_DIAGNOSTICS_H