#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include "rtmp_diagnostics.h"

#define MAX_LOG_SIZE (10 * 1024 * 1024) // 10MB
#define MAX_LOG_LINE 1024
#define LOG_FLUSH_INTERVAL 1000 // 1 segundo

typedef struct {
    FILE *log_file;
    pthread_mutex_t log_mutex;
    uint32_t last_flush;
    diagnostic_config_t config;
    diagnostic_stats_t stats;
    bool is_enabled;
} diagnostic_context_t;

static diagnostic_context_t g_context = {0};

// Inicialização do sistema de diagnósticos
bool rtmp_diagnostics_init(const char *log_path, diagnostic_config_t config) {
    if (g_context.is_enabled) return false;
    
    g_context.log_file = fopen(log_path, "a");
    if (!g_context.log_file) return false;
    
    pthread_mutex_init(&g_context.log_mutex, NULL);
    g_context.config = config;
    g_context.is_enabled = true;
    
    // Log inicial
    rtmp_diagnostics_log(LOG_INFO, "Diagnóstico RTMP iniciado - Versão 1.0");
    rtmp_diagnostics_log(LOG_INFO, "Configuração: Buffer=%d, QoS=%s, Failover=%s",
                        config.buffer_size,
                        config.qos_enabled ? "Ativado" : "Desativado",
                        config.failover_enabled ? "Ativado" : "Desativado");
    
    return true;
}

static const char *get_log_level_string(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO";
        case LOG_WARN:  return "WARN";
        case LOG_ERROR: return "ERROR";
        default:        return "UNKNOWN";
    }
}

static void get_timestamp(char *buffer, size_t size) {
    struct timeval tv;
    struct tm *tm;
    
    gettimeofday(&tv, NULL);
    tm = localtime(&tv.tv_sec);
    
    snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
             tm->tm_hour, tm->tm_min, tm->tm_sec,
             (int)(tv.tv_usec / 1000));
}

void rtmp_diagnostics_log(log_level_t level, const char *format, ...) {
    if (!g_context.is_enabled || level < g_context.config.min_log_level) return;
    
    char timestamp[32];
    char message[MAX_LOG_LINE];
    char final_message[MAX_LOG_LINE + 64];
    va_list args;
    
    get_timestamp(timestamp, sizeof(timestamp));
    
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    snprintf(final_message, sizeof(final_message), "[%s] [%s] %s\n",
             timestamp, get_log_level_string(level), message);
    
    pthread_mutex_lock(&g_context.log_mutex);
    
    // Escreve no arquivo
    fputs(final_message, g_context.log_file);
    
    // Atualiza estatísticas
    g_context.stats.total_logs++;
    if (level == LOG_ERROR) g_context.stats.error_count++;
    
    // Flush periódico
    uint32_t current_time = (uint32_t)time(NULL) * 1000;
    if (current_time - g_context.last_flush >= LOG_FLUSH_INTERVAL) {
        fflush(g_context.log_file);
        g_context.last_flush = current_time;
    }
    
    pthread_mutex_unlock(&g_context.log_mutex);
}

void rtmp_diagnostics_update_stream_stats(const stream_stats_t *stats) {
    if (!g_context.is_enabled) return;
    
    static uint32_t last_update = 0;
    uint32_t current_time = (uint32_t)time(NULL) * 1000;
    
    // Atualiza a cada segundo
    if (current_time - last_update >= 1000) {
        rtmp_diagnostics_log(LOG_DEBUG, 
            "Stream Stats - FPS: %.2f, Buffer: %.2f%%, Quality: %.2f",
            stats->current_fps,
            stats->buffer_usage * 100,
            stats->quality_score);
        
        last_update = current_time;
    }
}

void rtmp_diagnostics_update_network_stats(const network_stats_t *stats) {
    if (!g_context.is_enabled) return;
    
    static uint32_t last_update = 0;
    uint32_t current_time = (uint32_t)time(NULL) * 1000;
    
    if (current_time - last_update >= 1000) {
        rtmp_diagnostics_log(LOG_DEBUG,
            "Network Stats - Bandwidth: %.2f Mbps, Latency: %dms, Packet Loss: %.2f%%",
            stats->bandwidth_mbps,
            stats->latency_ms,
            stats->packet_loss * 100);
        
        last_update = current_time;
    }
}

void rtmp_diagnostics_record_event(diagnostic_event_t event, const char *details) {
    if (!g_context.is_enabled) return;
    
    const char *event_str = "Unknown";
    log_level_t level = LOG_INFO;
    
    switch (event) {
        case EVENT_STREAM_START:
            event_str = "Stream Started";
            break;
        case EVENT_STREAM_STOP:
            event_str = "Stream Stopped";
            break;
        case EVENT_QUALITY_CHANGE:
            event_str = "Quality Changed";
            break;
        case EVENT_FAILOVER:
            event_str = "Failover Activated";
            level = LOG_WARN;
            break;
        case EVENT_ERROR:
            event_str = "Error Occurred";
            level = LOG_ERROR;
            break;
    }
    
    rtmp_diagnostics_log(level, "Event: %s - %s", event_str, details ? details : "");
}

diagnostic_stats_t rtmp_diagnostics_get_stats(void) {
    diagnostic_stats_t stats = {0};
    
    if (g_context.is_enabled) {
        pthread_mutex_lock(&g_context.log_mutex);
        stats = g_context.stats;
        pthread_mutex_unlock(&g_context.log_mutex);
    }
    
    return stats;
}

void rtmp_diagnostics_shutdown(void) {
    if (!g_context.is_enabled) return;
    
    rtmp_diagnostics_log(LOG_INFO, "Diagnóstico RTMP finalizado");
    
    pthread_mutex_lock(&g_context.log_mutex);
    
    fflush(g_context.log_file);
    fclose(g_context.log_file);
    g_context.log_file = NULL;
    g_context.is_enabled = false;
    
    pthread_mutex_unlock(&g_context.log_mutex);
    pthread_mutex_destroy(&g_context.log_mutex);
}