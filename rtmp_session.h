#ifndef RTMP_SESSION_H
#define RTMP_SESSION_H

#include <stdint.h>
#include "rtmp_core.h"

// Estados da sessão
#define RTMP_SESSION_STATE_INIT       0
#define RTMP_SESSION_STATE_CONNECTING 1
#define RTMP_SESSION_STATE_CONNECTED  2
#define RTMP_SESSION_STATE_PUBLISHING 3
#define RTMP_SESSION_STATE_ERROR      4
#define RTMP_SESSION_STATE_CLOSED     5

// Configuração da sessão
typedef struct {
    char app_name[128];
    char stream_name[128];
    char stream_key[128];
    int chunk_size;
    int window_size;
    int ping_interval;
    void *user_data;
} rtmp_session_config_t;

// Estatísticas da sessão
typedef struct {
    int state;
    uint64_t connect_time;
    uint64_t publish_time;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint32_t messages_sent;
    uint32_t messages_received;
    uint32_t ping_count;
    float rtt;               // Round Trip Time
    float bandwidth_in;
    float bandwidth_out;
} rtmp_session_stats_t;

// Contexto da sessão (opaco)
typedef struct rtmp_session rtmp_session_t;

// Callbacks
typedef void (*rtmp_session_state_callback_t)(void *user_data, int old_state, int new_state);
typedef void (*rtmp_session_error_callback_t)(void *user_data, const char *error);

// Funções principais
rtmp_session_t* rtmp_session_create(const rtmp_session_config_t *config);
void rtmp_session_destroy(rtmp_session_t *session);

// Operações
int rtmp_session_connect(rtmp_session_t *session);
int rtmp_session_disconnect(rtmp_session_t *session);
int rtmp_session_start_publish(rtmp_session_t *session);
int rtmp_session_stop_publish(rtmp_session_t *session);

// Envio de dados
int rtmp_session_send_video(rtmp_session_t *session, const uint8_t *data, size_t size, int64_t timestamp);
int rtmp_session_send_audio(rtmp_session_t *session, const uint8_t *data, size_t size, int64_t timestamp);
int rtmp_session_send_metadata(rtmp_session_t *session, const char *name, const uint8_t *data, size_t size);

// Monitoramento
int rtmp_session_get_stats(rtmp_session_t *session, rtmp_session_stats_t *stats);
int rtmp_session_get_state(rtmp_session_t *session);

// Callbacks
void rtmp_session_set_state_callback(rtmp_session_t *session, rtmp_session_state_callback_t callback);
void rtmp_session_set_error_callback(rtmp_session_t *session, rtmp_session_error_callback_t callback);

#endif // RTMP_SESSION_H