#ifndef RTMP_PROTOCOL_H
#define RTMP_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

// Comandos RTMP
#define RTMP_COMMAND_CONNECT        "connect"
#define RTMP_COMMAND_CREATESTREAM   "createStream"
#define RTMP_COMMAND_PUBLISH        "publish"
#define RTMP_COMMAND_PLAY           "play"
#define RTMP_COMMAND_PAUSE          "pause"
#define RTMP_COMMAND_SEEK           "seek"
#define RTMP_COMMAND_DELETESTREAM   "deleteStream"
#define RTMP_COMMAND_CLOSESTREAM    "closeStream"
#define RTMP_COMMAND_RELEASESTREAM  "releaseStream"
#define RTMP_COMMAND_FCPUBLISH      "FCPublish"
#define RTMP_COMMAND_FCUNPUBLISH    "FCUnpublish"

// Tipos de mensagem RTMP
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

// Eventos de User Control
#define RTMP_USER_STREAM_BEGIN      0
#define RTMP_USER_STREAM_EOF        1
#define RTMP_USER_STREAM_DRY        2
#define RTMP_USER_SET_BUFFER_LENGTH 3
#define RTMP_USER_STREAM_IS_RECORDED 4
#define RTMP_USER_PING_REQUEST      6
#define RTMP_USER_PING_RESPONSE     7

// Funções para criar mensagens RTMP comuns
int rtmp_create_set_chunk_size(uint8_t* buffer, size_t len, uint32_t chunk_size);
int rtmp_create_window_ack_size(uint8_t* buffer, size_t len, uint32_t window_size);
int rtmp_create_set_peer_bandwidth(uint8_t* buffer, size_t len, uint32_t window_size, uint8_t limit_type);
int rtmp_create_user_control(uint8_t* buffer, size_t len, uint16_t event_type, uint32_t event_data);
int rtmp_create_command(uint8_t* buffer, size_t len, const char* command_name, double transaction_id, ...);

// Funções para processar mensagens RTMP
int rtmp_process_chunk_size(const uint8_t* data, size_t len, uint32_t* chunk_size);
int rtmp_process_window_ack_size(const uint8_t* data, size_t len, uint32_t* window_size);
int rtmp_process_set_peer_bandwidth(const uint8_t* data, size_t len, uint32_t* window_size, uint8_t* limit_type);
int rtmp_process_user_control(const uint8_t* data, size_t len, uint16_t* event_type, uint32_t* event_data);

#endif // RTMP_PROTOCOL_H