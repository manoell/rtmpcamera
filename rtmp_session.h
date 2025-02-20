// rtmp_session.h
#ifndef RTMP_SESSION_H
#define RTMP_SESSION_H

#include "rtmp_core.h"
#include "rtmp_chunk.h"
#include "rtmp_protocol.h"
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

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

// Estrutura da sessão
struct RTMPSession {
    int socket_fd;
    RTMPSessionState state;
    RTMPChunkStream* chunk_stream;
    RTMPStreamInfo stream_info;
    char app_name[128];
    char stream_name[128];
    uint32_t stream_id;
    bool is_running;
    pthread_t thread;
    pthread_mutex_t mutex;
    void* user_data;
    struct RTMPServer* server;  // Referência ao servidor
};

// Funções de gerenciamento de sessão
RTMPSession* rtmp_session_create(int socket_fd, struct RTMPServer* server);
void rtmp_session_destroy(RTMPSession* session);
int rtmp_session_start(RTMPSession* session);
void rtmp_session_stop(RTMPSession* session);

// Funções de processamento
int rtmp_session_process(RTMPSession* session);
void rtmp_session_handle_message(RTMPSession* session, RTMPMessage* message);

// Funções de informação do stream
void rtmp_session_update_stream_info(RTMPSession* session, RTMPMessage* message);
void rtmp_session_log_stats(RTMPSession* session);

#endif