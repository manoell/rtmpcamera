#ifndef RTMP_PROTOCOL_H
#define RTMP_PROTOCOL_H

#include "rtmp_core.h"
#include <stdint.h>

// Tipos de mensagens RTMP
#define RTMP_MSG_SET_CHUNK_SIZE     1
#define RTMP_MSG_ABORT              2
#define RTMP_MSG_ACKNOWLEDGEMENT    3
#define RTMP_MSG_USER_CONTROL       4
#define RTMP_MSG_WINDOW_ACK_SIZE    5
#define RTMP_MSG_SET_PEER_BANDWIDTH 6
#define RTMP_MSG_AUDIO             8
#define RTMP_MSG_VIDEO             9
#define RTMP_MSG_DATA_AMF3         15
#define RTMP_MSG_SHARED_OBJ_AMF3   16
#define RTMP_MSG_COMMAND_AMF3      17
#define RTMP_MSG_DATA_AMF0         18
#define RTMP_MSG_SHARED_OBJ_AMF0   19
#define RTMP_MSG_COMMAND_AMF0      20

#define RTMP_MSG_USER_CONTROL_STREAM_BEGIN      0x00
#define RTMP_MSG_USER_CONTROL_STREAM_EOF        0x01
#define RTMP_MSG_USER_CONTROL_STREAM_DRY        0x02
#define RTMP_MSG_USER_CONTROL_SET_BUFFER_LENGTH 0x03
#define RTMP_MSG_USER_CONTROL_STREAM_IS_RECORDED 0x04
#define RTMP_MSG_USER_CONTROL_PING_REQUEST      0x06
#define RTMP_MSG_USER_CONTROL_PING_RESPONSE     0x07

// Stream states
#define RTMP_STREAM_STATE_IDLE     0
#define RTMP_STREAM_STATE_RESERVED 1
#define RTMP_STREAM_STATE_ACTIVE   2
#define RTMP_STREAM_STATE_CLOSED   3

// Estrutura da mensagem RTMP
typedef struct {
    uint8_t type;              // Tipo da mensagem
    uint32_t timestamp;        // Timestamp
    uint32_t message_length;   // Tamanho da mensagem
    uint8_t message_type_id;   // ID do tipo de mensagem
    uint32_t stream_id;        // ID do stream
    uint8_t* payload;          // Dados da mensagem
    size_t payload_size;       // Tamanho do payload
} RTMPMessage;

// Forward declaration da estrutura RTMPSession
struct RTMPSession;

// Funções do protocolo
int rtmp_protocol_parse_message(uint8_t* data, size_t len, RTMPMessage* message);
int rtmp_protocol_create_message(RTMPMessage* message, uint8_t* buffer, size_t* len);
void rtmp_protocol_handle_message(struct RTMPSession* session, RTMPMessage* message);
void rtmp_message_free(RTMPMessage* message);

// Funções de handshake
int rtmp_handshake_perform(struct RTMPSession* session);

#endif