#ifndef RTMP_TYPES_H
#define RTMP_TYPES_H

#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

// Constantes RTMP
#define RTMP_DEFAULT_PORT 1935
#define RTMP_MAX_CHUNK_SIZE 128
#define RTMP_HANDSHAKE_SIZE 1536
#define RTMP_MAX_STREAMS 8
#define RTMP_DEFAULT_BUFFER_SIZE 2500000

// Tipos de mensagens RTMP
#define RTMP_MSG_SET_CHUNK_SIZE     1
#define RTMP_MSG_ABORT              2
#define RTMP_MSG_ACK                3
#define RTMP_MSG_USER_CONTROL       4
#define RTMP_MSG_WINDOW_ACK_SIZE    5
#define RTMP_MSG_SET_PEER_BW        6
#define RTMP_MSG_AUDIO             8
#define RTMP_MSG_VIDEO             9
#define RTMP_MSG_AMF3_DATA         15
#define RTMP_MSG_AMF3_SHARED_OBJ   16
#define RTMP_MSG_AMF3_COMMAND      17
#define RTMP_MSG_AMF_DATA          18
#define RTMP_MSG_AMF_SHARED_OBJ    19
#define RTMP_MSG_AMF_COMMAND       20

// Estados RTMP
typedef enum {
    RTMP_STATE_INIT = 0,
    RTMP_STATE_HANDSHAKE_C0C1,
    RTMP_STATE_HANDSHAKE_C2,
    RTMP_STATE_CONNECTED,
    RTMP_STATE_STREAMING,
    RTMP_STATE_ERROR
} rtmp_state_t;

// Estrutura de stream
typedef struct {
    uint32_t id;
    uint8_t type;
    uint8_t active;
    void* data;
    uint32_t data_size;
    uint32_t timestamp;
} rtmp_stream_t;

// Estrutura de chunk
typedef struct {
    uint8_t type;
    uint32_t timestamp;
    uint32_t size;
    uint8_t msg_type_id;
    uint32_t stream_id;
    uint8_t* data;
} rtmp_chunk_t;

// Estrutura de sessão
typedef struct {
    int socket;
    struct sockaddr_in addr;
    rtmp_state_t state;
    uint8_t connected;
    
    // Buffers
    uint8_t* in_buffer;
    uint32_t in_buffer_size;
    uint8_t* out_buffer;
    uint32_t out_buffer_size;
    
    // Chunk size
    uint32_t in_chunk_size;
    uint32_t out_chunk_size;
    
    // Window size
    uint32_t window_size;
    uint32_t bytes_in;
    uint32_t bytes_out;
    uint32_t last_ack;
    
    // Streams
    rtmp_stream_t streams[RTMP_MAX_STREAMS];
    uint32_t stream_count;
    
    // Preview
    uint8_t preview_enabled;
    void* preview_data;
} rtmp_session_t;

// Estrutura de pacote
typedef struct {
    uint8_t type;
    uint32_t timestamp;
    uint32_t size;
    uint32_t stream_id;
    uint8_t* data;
    uint32_t data_size;
} rtmp_packet_t;

#endif // RTMP_TYPES_H