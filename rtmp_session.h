#ifndef RTMP_SESSION_H
#define RTMP_SESSION_H

#include <stdint.h>
#include "rtmp_chunk.h"

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

typedef struct RTMPSession RTMPSession;

RTMPSession* rtmp_session_create(void);
void rtmp_session_destroy(RTMPSession* session);
int rtmp_session_process_chunk(RTMPSession* session, RTMPChunk* chunk);
int rtmp_session_send_chunk(RTMPSession* session, RTMPChunk* chunk);
int rtmp_session_handle_connect(RTMPSession* session, const uint8_t* data, size_t len);
int rtmp_session_handle_createStream(RTMPSession* session, const uint8_t* data, size_t len);
int rtmp_session_handle_publish(RTMPSession* session, const uint8_t* data, size_t len);
int rtmp_session_handle_play(RTMPSession* session, const uint8_t* data, size_t len);

#endif // RTMP_SESSION_H