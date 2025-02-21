// rtmp_core.h
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
#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif

// Definições de log
#define RTMP_LOG_FILE "/var/tmp/rtmp_debug.log"
#define RTMP_MAX_CONNECTIONS 10
#define RTMP_BUFFER_SIZE 4096

// Enums e defines
typedef enum {
    RTMP_LOG_DEBUG,
    RTMP_LOG_INFO,
    RTMP_LOG_WARNING,
    RTMP_LOG_ERROR
} RTMPLogLevel;

// Códigos de retorno
#define RTMP_OK 0
#define RTMP_ERROR_SOCKET -1
#define RTMP_ERROR_BIND -2
#define RTMP_ERROR_LISTEN -3
#define RTMP_ERROR_HANDSHAKE -4
#define RTMP_ERROR_MEMORY -5
#define RTMP_ERROR_PROTOCOL -6

// Estados da sessão RTMP
#define RTMP_SESSION_CREATED    0
#define RTMP_SESSION_HANDSHAKE  1
#define RTMP_SESSION_CONNECTING 2
#define RTMP_SESSION_CONNECTED  3
#define RTMP_SESSION_PUBLISHING 4
#define RTMP_SESSION_ERROR      5
#define RTMP_SESSION_CLOSED     6

// Forward declarations
struct RTMPServer;
struct RTMPSession;
struct RTMPMessage;
struct RTMPChunkStream;
struct RTMPClient;

// Estrutura do Cliente RTMP
typedef struct RTMPClient {
    int socket_fd;
    struct sockaddr_in addr;
    uint8_t *buffer;
    size_t buffer_size;
    bool connected;
    pthread_t thread;
    void* user_data;
} RTMPClient;

// Primeiro definir a estrutura de mensagem
typedef struct RTMPMessage {
    uint8_t type;
    uint32_t timestamp;
    uint32_t message_length;
    uint8_t message_type_id;
    uint32_t stream_id;
    uint8_t* payload;
} RTMPMessage;

// Depois a estrutura de chunk data
typedef struct RTMPChunkData {
    uint8_t* data;
    size_t size;
    size_t capacity;
    uint32_t timestamp;
} RTMPChunkData;

// Seguida da estrutura de chunk stream
typedef struct RTMPChunkStream {
    RTMPChunkData chunks[64];
    uint32_t chunk_size;
    uint32_t window_size;
    uint32_t bytes_received;
    uint32_t last_ack;
    int socket_fd;
} RTMPChunkStream;

// Estrutura de informações do stream
typedef struct RTMPStreamInfo {
    uint32_t width;
    uint32_t height;
    double fps;
    uint32_t video_bitrate;
    uint32_t audio_bitrate;
    char video_codec[32];
    char audio_codec[32];
    uint64_t start_time;
    uint32_t total_frames;
    float network_latency;
} RTMPStreamInfo;

// Finalmente a estrutura de sessão
typedef struct RTMPSession {
    int socket_fd;
    uint8_t state;
    uint32_t chunk_size;
    uint32_t window_size;
    uint32_t bytes_received;
    uint32_t last_ack;
    RTMPMessage* current_message;
    RTMPChunkStream* chunk_stream;
    struct RTMPServer* server;
    RTMPStreamInfo stream_info;
    pthread_t thread;
    pthread_mutex_t mutex;
    bool is_running;
    void* user_data;
} RTMPSession;

// Estrutura do servidor
typedef struct RTMPServer {
    int socket_fd;
    bool running;
    pthread_t accept_thread;
    struct sockaddr_in addr;
    RTMPClient* clients;  // Array de clientes
    int client_count;
    pthread_mutex_t clients_mutex;
} RTMPServer;

// Protótipos das funções
void rtmp_log(RTMPLogLevel level, const char* format, ...);
RTMPServer* rtmp_server_create(int port);
void rtmp_server_destroy(RTMPServer* server);
int rtmp_server_start(RTMPServer* server);
void rtmp_server_stop(RTMPServer* server);
void rtmp_client_handle(RTMPClient* client);
int rtmp_server_add_client(RTMPServer* server, int client_fd, struct sockaddr_in addr);
void rtmp_server_remove_client(RTMPServer* server, int client_index);

#ifdef __cplusplus
}
#endif

#endif // RTMP_CORE_H