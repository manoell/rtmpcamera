// rtmp_protocol.h
#ifndef RTMP_PROTOCOL_H
#define RTMP_PROTOCOL_H

#include "rtmp_core.h"
#include <stdint.h>

// Tipos de mensagens RTMP
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

// Tipos de eventos de controle do usuário
#define RTMP_USER_STREAM_BEGIN      0
#define RTMP_USER_STREAM_EOF        1
#define RTMP_USER_STREAM_DRY        2
#define RTMP_USER_SET_BUFFER_LENGTH 3
#define RTMP_USER_STREAM_IS_RECORDED 4
#define RTMP_USER_PING_REQUEST      6
#define RTMP_USER_PING_RESPONSE     7

typedef struct {
    uint8_t type;
    uint32_t timestamp;
    uint32_t message_length;
    uint8_t message_type_id;
    uint32_t stream_id;
    uint8_t* payload;
} RTMPMessage;

typedef struct {
    uint8_t fmt;
    uint32_t csid;
    RTMPMessage message;
} RTMPChunkHeader;

// Funções de protocolo
int rtmp_protocol_parse_message(uint8_t* data, size_t len, RTMPMessage* message);
int rtmp_protocol_create_message(RTMPMessage* message, uint8_t* buffer, size_t* len);
void rtmp_protocol_handle_message(RTMPSession* session, RTMPMessage* message);
void rtmp_message_free(RTMPMessage* message);

// Funções de handshake
int rtmp_handshake_perform(RTMPSession* session);

#endif