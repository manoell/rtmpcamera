#ifndef RTMP_CORE_H
#define RTMP_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>

// Configurações otimizadas
#define RTMP_DEFAULT_PORT 1935
#define RTMP_DEFAULT_CHUNK_SIZE 4096
#define RTMP_MAX_PACKET_SIZE (1024 * 1024)
#define RTMP_BUFFER_TIME 500
#define RTMP_DEFAULT_TIMEOUT 5000

// Tipos de pacotes RTMP
typedef enum {
    RTMP_PACKET_TYPE_CHUNK_SIZE = 1,
    RTMP_PACKET_TYPE_BYTES_READ = 3,
    RTMP_PACKET_TYPE_PING = 4,
    RTMP_PACKET_TYPE_SERVER_BW = 5,
    RTMP_PACKET_TYPE_CLIENT_BW = 6,
    RTMP_PACKET_TYPE_AUDIO = 8,
    RTMP_PACKET_TYPE_VIDEO = 9,
    RTMP_PACKET_TYPE_METADATA = 18,
    RTMP_PACKET_TYPE_INVOKE = 20,
} rtmp_packet_type_t;

// Estrutura de pacote RTMP
typedef struct {
    uint8_t channel_id;
    uint32_t timestamp;
    uint32_t length;
    rtmp_packet_type_t type;
    uint32_t stream_id;
    uint8_t *data;
} rtmp_packet_t;

// Estado da conexão
typedef struct {
    int socket;
    bool is_connected;
    uint32_t chunk_size;
    uint32_t stream_id;
    uint32_t sequence_number;
    uint32_t timestamp;
    uint32_t window_size;
    uint32_t bandwidth;
} rtmp_connection_t;

// Estrutura de sessão RTMP
typedef struct {
    rtmp_connection_t *connection;
    char *app_name;
    char *stream_name;
    char *swf_url;
    char *tcp_url;
    uint32_t buffer_length;
    bool is_playing;
    void *user_data;
} rtmp_session_t;

// Callbacks
typedef void (*rtmp_callback_t)(rtmp_session_t *session, rtmp_packet_t *packet);

// Funções principais
rtmp_session_t *rtmp_session_create(void);
void rtmp_session_destroy(rtmp_session_t *session);

int rtmp_connect(rtmp_session_t *session, const char *url);
void rtmp_disconnect(rtmp_session_t *session);

int rtmp_read_packet(rtmp_session_t *session, rtmp_packet_t *packet);
int rtmp_write_packet(rtmp_session_t *session, rtmp_packet_t *packet);

void rtmp_set_callback(rtmp_session_t *session, rtmp_callback_t callback);

// Funções de controle
void rtmp_set_chunk_size(rtmp_session_t *session, uint32_t size);
void rtmp_set_buffer_length(rtmp_session_t *session, uint32_t length);
void rtmp_set_window_size(rtmp_session_t *session, uint32_t size);
void rtmp_set_bandwidth(rtmp_session_t *session, uint32_t bandwidth);

// Funções de status
bool rtmp_is_connected(rtmp_session_t *session);
uint32_t rtmp_get_time(void);
const char *rtmp_get_error_string(int error);

// Funções de debug
void rtmp_set_debug(bool enable);
void rtmp_dump_packet(rtmp_packet_t *packet);

#endif // RTMP_CORE_H