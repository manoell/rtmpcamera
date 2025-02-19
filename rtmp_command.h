#ifndef RTMP_COMMAND_H
#define RTMP_COMMAND_H

#include <stdint.h>
#include <stdlib.h>

// Precisamos incluir as definições dos tipos
#include "rtmp_types.h"

// RTMP Command types
#define RTMP_CMD_CONNECT       "connect"
#define RTMP_CMD_CREATE_STREAM "createStream"
#define RTMP_CMD_PLAY          "play"
#define RTMP_CMD_PAUSE         "pause"
#define RTMP_CMD_SEEK          "seek"
#define RTMP_CMD_ON_STATUS     "onStatus"
#define RTMP_CMD_RESULT        "_result"
#define RTMP_CMD_ERROR         "_error"

// RTMP command functions
int rtmp_process_command(rtmp_session_t *session, rtmp_packet_t *packet);
int rtmp_handle_connect(rtmp_session_t *session, rtmp_packet_t *packet);
int rtmp_handle_create_stream(rtmp_session_t *session, rtmp_packet_t *packet);
int rtmp_handle_publish(rtmp_session_t *session, rtmp_packet_t *packet);
int rtmp_handle_play(rtmp_session_t *session, rtmp_packet_t *packet);
int rtmp_handle_pause(rtmp_session_t *session, rtmp_packet_t *packet);
int rtmp_handle_seek(rtmp_session_t *session, rtmp_packet_t *packet);
int rtmp_handle_delete_stream(rtmp_session_t *session, rtmp_packet_t *packet);

// Helper functions
int rtmp_send_connect_result(rtmp_session_t *session);
int rtmp_send_stream_begin(rtmp_session_t *session);
int rtmp_send_create_stream_result(rtmp_session_t *session, double transaction_id, double stream_id);
int rtmp_create_stream_id(rtmp_session_t *session);
int rtmp_send_control_packet(rtmp_session_t *session, uint8_t type, uint32_t value);
int rtmp_packet_send(rtmp_session_t *session, rtmp_packet_t *packet);

#endif /* RTMP_COMMAND_H */