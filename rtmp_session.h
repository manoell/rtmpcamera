#ifndef RTMP_SESSION_H
#define RTMP_SESSION_H

#include <stdint.h>
#include "rtmp_chunk.h"

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