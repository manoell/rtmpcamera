#ifndef RTMP_SERVER_INTEGRATION_H
#define RTMP_SERVER_INTEGRATION_H

#include <stdint.h>
#include <stdbool.h>

// Eventos do servidor
typedef enum {
    SERVER_CONNECTED,
    SERVER_DISCONNECTED,
    SERVER_STREAM_START,
    SERVER_STREAM_END,
    SERVER_ERROR
} server_event_t;

// Configuração do servidor
typedef struct {
    uint32_t chunk_size;
    uint32_t window_ack_size;
    uint32_t peer_bandwidth;
    uint32_t max_bitrate;
    uint32_t min_bitrate;
    uint32_t target_latency;
    float quality_priority;
    const char *backup_url;
    void (*callback)(server_event_t event, void *data, void *ctx);
    void *callback_ctx;
} server_config_t;

// Estatísticas do servidor
typedef struct {
    bool is_connected;
    uint32_t current_bitrate;
    float buffer_health;
    float current_fps;
    uint32_t failover_count;
} server_stats_t;

// Handle opaco para o contexto do servidor
typedef struct rtmp_server_context rtmp_server_context_t;

// Funções principais
rtmp_server_context_t *rtmp_server_create(const char *url, uint16_t port);
int rtmp_server_connect(rtmp_server_context_t *context);
int rtmp_server_reconnect(rtmp_server_context_t *context);
void rtmp_server_configure(rtmp_server_context_t *context, const server_config_t *config);
server_stats_t rtmp_server_get_stats(rtmp_server_context_t *context);
void rtmp_server_destroy(rtmp_server_context_t *context);

#endif // RTMP_SERVER_INTEGRATION_H