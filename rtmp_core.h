#ifndef RTMP_CORE_H
#define RTMP_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// RTMP Message Types
#define RTMP_MSG_CHUNK_SIZE     1
#define RTMP_MSG_ABORT          2
#define RTMP_MSG_ACK            3
#define RTMP_MSG_USER_CONTROL   4
#define RTMP_MSG_WINDOW_ACK     5
#define RTMP_MSG_SET_PEER_BW    6
#define RTMP_MSG_AUDIO          8
#define RTMP_MSG_VIDEO          9
#define RTMP_MSG_DATA_AMF3      15
#define RTMP_MSG_SHARED_OBJ_AMF3 16
#define RTMP_MSG_COMMAND_AMF3   17
#define RTMP_MSG_DATA_AMF0      18
#define RTMP_MSG_SHARED_OBJ_AMF0 19
#define RTMP_MSG_COMMAND_AMF0   20
#define RTMP_MSG_AGGREGATE      22

// RTMP Chunk Types
#define RTMP_CHUNK_TYPE_0       0  // 11 bytes
#define RTMP_CHUNK_TYPE_1       1  // 7 bytes
#define RTMP_CHUNK_TYPE_2       2  // 3 bytes
#define RTMP_CHUNK_TYPE_3       3  // 0 bytes

// RTMP States
#define RTMP_STATE_INIT         0
#define RTMP_STATE_HANDSHAKE    1
#define RTMP_STATE_CONNECT      2
#define RTMP_STATE_CONNECTED    3
#define RTMP_STATE_CREATE_STREAM 4
#define RTMP_STATE_READY        5
#define RTMP_STATE_PLAY         6
#define RTMP_STATE_PUBLISH      7

// RTMP Events
#define RTMP_EVENT_STREAM_BEGIN      0
#define RTMP_EVENT_STREAM_EOF        1
#define RTMP_EVENT_STREAM_DRY        2
#define RTMP_EVENT_SET_BUFFER_LENGTH 3
#define RTMP_EVENT_STREAM_IS_RECORDED 4
#define RTMP_EVENT_PING_REQUEST      6
#define RTMP_EVENT_PING_RESPONSE     7

// Callbacks
typedef int (*RTMPVideoCallback)(void* userdata, const uint8_t* data, size_t len, uint32_t timestamp);
typedef int (*RTMPAudioCallback)(void* userdata, const uint8_t* data, size_t len, uint32_t timestamp);
typedef void (*RTMPEventCallback)(void* userdata, uint16_t event_type, uint32_t event_data);

// Forward declarations
typedef struct RTMPContext RTMPContext;
typedef struct RTMPChunk RTMPChunk;
typedef struct RTMPSession RTMPSession;

struct RTMPContext {
    int socket;                 // Socket associado
    uint8_t state;             // Estado atual da conexão
    uint32_t chunk_size;       // Tamanho do chunk atual
    uint32_t window_size;      // Tamanho da janela de acknowledgement
    uint32_t peer_bandwidth;   // Largura de banda do peer
    uint32_t stream_id;        // ID do stream atual
    
    // Buffer de recebimento
    uint8_t* buffer;
    size_t buffer_size;
    size_t buffer_used;
    
    // Callbacks
    RTMPVideoCallback video_callback;
    RTMPAudioCallback audio_callback;
    RTMPEventCallback event_callback;
    void* video_userdata;
    void* audio_userdata;
    void* event_userdata;
    
    // Estatísticas
    uint32_t bytes_received;
    uint32_t bytes_sent;
    uint32_t last_ack_time;
    uint32_t last_ping_time;
    
    // Dados do stream
    char app[128];
    char stream_key[128];
    int is_publishing;
    int is_playing;
};

// Funções de inicialização/destruição
RTMPContext* rtmp_context_create(void);
void rtmp_context_destroy(RTMPContext* ctx);

// Funções do servidor
int rtmp_server_start(int port);
void rtmp_server_stop(void);

// Funções de callback
void rtmp_set_video_callback(RTMPContext* ctx, RTMPVideoCallback cb, void* userdata);
void rtmp_set_audio_callback(RTMPContext* ctx, RTMPAudioCallback cb, void* userdata);
void rtmp_set_event_callback(RTMPContext* ctx, RTMPEventCallback cb, void* userdata);

// Funções de processamento
int rtmp_process_packet(RTMPContext* ctx, const uint8_t* data, size_t len);
int rtmp_send_packet(RTMPContext* ctx, uint8_t type, const uint8_t* data, size_t len);

// Funções de controle
int rtmp_send_ping(RTMPContext* ctx);
int rtmp_send_ack(RTMPContext* ctx);
int rtmp_send_window_ack_size(RTMPContext* ctx, uint32_t size);
int rtmp_send_set_chunk_size(RTMPContext* ctx, uint32_t size);
int rtmp_send_user_control(RTMPContext* ctx, uint16_t event_type, uint32_t event_data);

#ifdef __cplusplus
}
#endif

#endif // RTMP_CORE_H