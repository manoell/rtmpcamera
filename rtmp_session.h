// rtmp_session.h
#ifndef RTMP_SESSION_H
#define RTMP_SESSION_H

#include "rtmp_core.h"
#include "rtmp_chunk.h"
#include "rtmp_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif