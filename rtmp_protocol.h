#ifndef RTMP_PROTOCOL_H
#define RTMP_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include "rtmp_core.h"

// Message types
#define RTMP_MSG_CHUNK_SIZE     1
#define RTMP_MSG_ABORT          2
#define RTMP_MSG_ACKNOWLEDGEMENT 3
#define RTMP_MSG_USER_CONTROL   4
#define RTMP_MSG_WINDOW_ACK_SIZE 5
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

// User control message types
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

// Protocol functions
int rtmp_process_input(rtmp_session_t *session, const uint8_t *data, size_t size);
int rtmp_process_message(rtmp_session_t *session, rtmp_chunk_t *chunk);
int rtmp_send_message(rtmp_session_t *session, uint8_t msg_type_id, uint32_t msg_stream_id, const uint8_t *data, size_t size);
int rtmp_send_user_control(rtmp_session_t *session, uint16_t event_type, uint32_t event_data);
int rtmp_send_window_acknowledgement_size(rtmp_session_t *session, uint32_t window_size);
int rtmp_send_set_peer_bandwidth(rtmp_session_t *session, uint32_t window_size, uint8_t limit_type);
int rtmp_send_chunk_size(rtmp_session_t *session, uint32_t chunk_size);

#endif // RTMP_PROTOCOL_H