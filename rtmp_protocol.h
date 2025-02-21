// rtmp_protocol.h
#ifndef RTMP_PROTOCOL_H
#define RTMP_PROTOCOL_H

#include "rtmp_core.h"
#include <stdint.h>

// Definições de tipos de mensagem RTMP
#define RTMP_MSG_SET_CHUNK_SIZE     1
#define RTMP_MSG_ABORT              2
#define RTMP_MSG_ACKNOWLEDGEMENT    3
#define RTMP_MSG_USER_CONTROL       4
#define RTMP_MSG_WINDOW_ACK_SIZE    5
#define RTMP_MSG_SET_PEER_BANDWIDTH 6
#define RTMP_MSG_AUDIO             8
#define RTMP_MSG_VIDEO             9
#define RTMP_MSG_DATA_AMF3         15
#define RTMP_MSG_SHARED_OBJECT_AMF3 16
#define RTMP_MSG_COMMAND_AMF3      17
#define RTMP_MSG_DATA_AMF0         18
#define RTMP_MSG_SHARED_OBJECT_AMF0 19
#define RTMP_MSG_COMMAND_AMF0      20

// Funções de protocolo
int rtmp_protocol_parse_message(uint8_t* data, size_t len, RTMPMessage* message);
int rtmp_protocol_create_message(RTMPMessage* message, uint8_t* buffer, size_t* len);
void rtmp_protocol_handle_message(RTMPSession* session, RTMPMessage* message);
void rtmp_message_free(RTMPMessage* message);

// Funções de handshake
int rtmp_handshake_perform(RTMPSession* session);

#endif