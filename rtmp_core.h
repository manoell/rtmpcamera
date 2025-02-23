#ifndef RTMP_CORE_H
#define RTMP_CORE_H

#include <stdint.h>
#include <stddef.h>

// Configurações RTMP
#define RTMP_DEFAULT_PORT 1935
#define RTMP_DEFAULT_CHUNK_SIZE 128
#define RTMP_MAX_CHUNK_SIZE 65536
#define RTMP_DEFAULT_WINDOW_SIZE 2500000
#define RTMP_DEFAULT_BUFFER_TIME 500
#define RTMP_MAX_STREAMS 8

// Estados da conexão
typedef enum {
    RTMP_STATE_DISCONNECTED = 0,
    RTMP_STATE_CONNECTING,
    RTMP_STATE_HANDSHAKING,
    RTMP_STATE_CONNECTED,
    RTMP_STATE_PUBLISHING,
    RTMP_STATE_ERROR
} rtmp_state_t;

// Tipos de mensagem
typedef enum {
    RTMP_MSG_CHUNK_SIZE = 1,
    RTMP_MSG_ABORT = 2,
    RTMP_MSG_ACK = 3,
    RTMP_MSG_USER_CONTROL = 4,
    RTMP_MSG_WINDOW_ACK_SIZE = 5,
    RTMP_MSG_SET_PEER_BW = 6,
    RTMP_MSG_AUDIO = 8,
    RTMP_MSG_VIDEO = 9,
    RTMP_MSG_DATA_AMF3 = 15,
    RTMP_MSG_SHARED_OBJ_AMF3 = 16,
    RTMP_MSG_COMMAND_AMF3 = 17,
    RTMP_MSG_DATA_AMF0 = 18,
    RTMP_MSG_SHARED_OBJ_AMF0 = 19,
    RTMP_MSG_COMMAND_AMF0 = 20,
    RTMP_MSG_AGGREGATE = 22
} rtmp_message_type_t;

// Configuração da conexão
typedef struct {
    char host[256];
    int port;
    char app[128];
    char stream_key[128];
    int chunk_size;
    int window_size;
    int buffer_time;
    void *user_data;
} rtmp_config_t;

// Estrutura de conexão (opaca)
typedef struct rtmp_connection rtmp_connection_t;

// Callbacks
typedef void (*rtmp_state_callback_t)(void *user_data, rtmp_state_t old_state, rtmp_state_t new_state);
typedef void (*rtmp_error_callback_t)(void *user_data, const char *error);

// Funções principais
rtmp_connection_t* rtmp_create(const rtmp_config_t *config);
void rtmp_destroy(rtmp_connection_t *conn);
int rtmp_connect(rtmp_connection_t *conn);
void rtmp_disconnect(rtmp_connection_t *conn);
int rtmp_is_connected(rtmp_connection_t *conn);

// Funções de streaming
int rtmp_publish_start(rtmp_connection_t *conn);
int rtmp_publish_stop(rtmp_connection_t *conn);
int rtmp_send_video(rtmp_connection_t *conn, const uint8_t *data, size_t size, int64_t timestamp);
int rtmp_send_audio(rtmp_connection_t *conn, const uint8_t *data, size_t size, int64_t timestamp);
int rtmp_send_metadata(rtmp_connection_t *conn, const char *name, const uint8_t *data, size_t size);

// Callbacks e eventos
void rtmp_set_state_callback(rtmp_connection_t *conn, rtmp_state_callback_t callback);
void rtmp_set_error_callback(rtmp_connection_t *conn, rtmp_error_callback_t callback);

// Configuração
int rtmp_set_chunk_size(rtmp_connection_t *conn, int size);
int rtmp_set_window_size(rtmp_connection_t *conn, int size);
int rtmp_set_buffer_time(rtmp_connection_t *conn, int time_ms);

// Estatísticas e diagnóstico
typedef struct {
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t messages_sent;
    uint64_t messages_received;
    int current_chunk_size;
    int current_window_size;
    int current_buffer_time;
    rtmp_state_t state;
    uint64_t connect_time;
    uint64_t last_send_time;
    uint64_t last_receive_time;
    float bandwidth_in;
    float bandwidth_out;
} rtmp_stats_t;

int rtmp_get_stats(rtmp_connection_t *conn, rtmp_stats_t *stats);

#endif // RTMP_CORE_H