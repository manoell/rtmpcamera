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
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Definições de log
#define RTMP_LOG_FILE "/var/tmp/rtmp_debug.log"
#define RTMP_MAX_CONNECTIONS 10

// Estados da sessão
typedef enum {
    RTMP_SESSION_CREATED,
    RTMP_SESSION_HANDSHAKE,
    RTMP_SESSION_CONNECTING,
    RTMP_SESSION_CONNECTED,
    RTMP_SESSION_PUBLISHING,
    RTMP_SESSION_ERROR,
    RTMP_SESSION_CLOSED
} RTMPSessionState;

// Tipos de log
typedef enum {
    RTMP_LOG_DEBUG,
    RTMP_LOG_INFO,
    RTMP_LOG_WARNING,
    RTMP_LOG_ERROR
} RTMPLogLevel;

// Informações de stream
typedef struct {
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

// Estrutura do ChunkStream
typedef struct RTMPChunkData {
    uint8_t* data;
    size_t size;
    size_t capacity;
    uint32_t timestamp;
} RTMPChunkData;

typedef struct RTMPChunkStream {
    RTMPChunkData chunks[64];
    uint32_t chunk_size;
    uint32_t window_size;
    uint32_t bytes_received;
    uint32_t last_ack;
    int socket_fd;
} RTMPChunkStream;

// Forward declaration
typedef struct RTMPServer RTMPServer;

// Estrutura da sessão completa
typedef struct RTMPSession {
    int socket_fd;
    RTMPSessionState state;
    RTMPChunkStream* chunk_stream;
    RTMPStreamInfo stream_info;
    bool is_running;
    pthread_t thread;
    pthread_mutex_t mutex;
    struct RTMPServer* server;
    char app_name[128];
    char stream_name[128];
    uint32_t stream_id;
} RTMPSession;

// Códigos de retorno
#define RTMP_OK 0
#define RTMP_ERROR_SOCKET -1
#define RTMP_ERROR_BIND -2
#define RTMP_ERROR_LISTEN -3
#define RTMP_ERROR_HANDSHAKE -4
#define RTMP_ERROR_MEMORY -5
#define RTMP_ERROR_PROTOCOL -6

// Funções de log e servidor
void rtmp_log(RTMPLogLevel level, const char* format, ...);
RTMPServer* rtmp_server_create(int port);
void rtmp_server_destroy(RTMPServer* server);
int rtmp_server_start(RTMPServer* server);
void rtmp_server_stop(RTMPServer* server);

#ifdef __cplusplus
}
#endif

#endif