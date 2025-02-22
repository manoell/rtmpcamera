#ifndef RTMP_CORE_H
#define RTMP_CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Definições de log
#define RTMP_LOG_FILE "/var/tmp/rtmp_debug.log"
typedef void (*rtmp_audio_callback)(rtmp_session_t *session, const uint8_t *data, size_t size, uint32_t timestamp);
typedef void (*rtmp_video_callback)(rtmp_session_t *session, const uint8_t *data, size_t size, uint32_t timestamp);
typedef void (*rtmp_metadata_callback)(rtmp_session_t *session, const uint8_t *data, size_t size);

struct rtmp_session_s {
    // ... existing members ...
    
    rtmp_audio_callback audio_callback;
    rtmp_video_callback video_callback;
    rtmp_metadata_callback metadata_callback;
    
    // Buffer management
    uint8_t *aac_sequence_header;
    size_t aac_sequence_header_size;
    uint8_t *avc_sequence_header;
    size_t avc_sequence_header_size;
    
    // Connection state
    uint32_t window_ack_size;
    uint32_t peer_bandwidth;
    uint8_t peer_bandwidth_limit_type;
    uint32_t last_ack_received;
};

#define RTMP_MAX_CONNECTIONS 10

struct rtmp_server_s {
    // ... existing members ...
    
    rtmp_session_t *connections[RTMP_MAX_CONNECTIONS];
    int num_connections;
    pthread_mutex_t connections_mutex;
};

// Tipos de mensagens RTMP
#define RTMP_MSG_CHUNK_SIZE        1
#define RTMP_MSG_ABORT            2
#define RTMP_MSG_ACK              3
#define RTMP_MSG_USER_CONTROL     4
#define RTMP_MSG_WINDOW_ACK_SIZE  5
#define RTMP_MSG_SET_PEER_BW      6
#define RTMP_MSG_AUDIO            8
#define RTMP_MSG_VIDEO            9
#define RTMP_MSG_AMF3_DATA       15
#define RTMP_MSG_AMF3_SHARED     16
#define RTMP_MSG_AMF3_CMD        17
#define RTMP_MSG_AMF0_DATA       18
#define RTMP_MSG_AMF0_SHARED     19
#define RTMP_MSG_AMF0_CMD        20

// Estados RTMP
#define RTMP_STATE_UNINITIALIZED   0
#define RTMP_STATE_VERSION_SENT    1
#define RTMP_STATE_ACK_SENT       2
#define RTMP_STATE_HANDSHAKE_DONE 3
#define RTMP_STATE_CONNECT        4
#define RTMP_STATE_CONNECTED      5
#define RTMP_STATE_CREATE_STREAM  6
#define RTMP_STATE_READY         7

// Tipos de log
typedef enum {
    RTMP_LOG_DEBUG,
    RTMP_LOG_INFO,
    RTMP_LOG_WARNING,
    RTMP_LOG_ERROR
} RTMPLogLevel;

// Estrutura da mensagem RTMP
typedef struct {
    uint8_t type;
    uint32_t timestamp;
    uint32_t message_length;
    uint8_t message_type_id;
    uint32_t stream_id;
    uint8_t* payload;
    size_t payload_size;
} RTMPMessage;

// Estrutura do cliente RTMP
typedef struct {
    int socket_fd;
    struct sockaddr_in addr;
    uint8_t* in_buffer;
    uint8_t* out_buffer;
    size_t in_buffer_size;
    size_t out_buffer_size;
    int state;
    pthread_t thread;
    bool running;
    void* user_data;
    uint32_t chunk_size;
    uint32_t window_size;
    uint32_t bandwidth;
    uint8_t bandwidth_limit_type;
    uint32_t bytes_in;
    uint32_t bytes_out;
    RTMPMessage* current_message;
} RTMPClient;

// Estrutura do servidor
typedef struct {
    int socket_fd;
    bool running;
    pthread_t accept_thread;
    struct sockaddr_in addr;
    RTMPClient* clients[RTMP_MAX_CONNECTIONS];
    int client_count;
    pthread_mutex_t clients_mutex;
} RTMPServer;

// Códigos de retorno
#define RTMP_OK 0
#define RTMP_ERROR_SOCKET -1
#define RTMP_ERROR_BIND -2
#define RTMP_ERROR_LISTEN -3
#define RTMP_ERROR_HANDSHAKE -4
#define RTMP_ERROR_MEMORY -5
#define RTMP_ERROR_PROTOCOL -6

// Funções públicas
void rtmp_log(RTMPLogLevel level, const char* format, ...);
RTMPServer* rtmp_server_create(int port);
void rtmp_server_destroy(RTMPServer* server);
int rtmp_server_start(RTMPServer* server);
void rtmp_server_stop(RTMPServer* server);

// Função de handshake
int rtmp_handshake_perform(RTMPClient* client);

#ifdef __cplusplus
}
#endif

#endif