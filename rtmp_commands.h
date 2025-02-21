#ifndef RTMP_COMMANDS_H
#define RTMP_COMMANDS_H

#include <stdint.h>
#include <stddef.h>
#include "rtmp_core.h"

// Default settings
#define RTMP_DEFAULT_WINDOW_SIZE 2500000
#define RTMP_DEFAULT_CHUNK_SIZE 128

// User control message types
#define RTMP_USER_STREAM_BEGIN      0
#define RTMP_USER_STREAM_EOF        1
#define RTMP_USER_STREAM_DRY        2
#define RTMP_USER_SET_BUFFER_LENGTH 3
#define RTMP_USER_STREAM_IS_RECORDED 4
#define RTMP_USER_PING_REQUEST      6
#define RTMP_USER_PING_RESPONSE     7

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

// Command handling functions
int rtmp_handle_command(rtmp_session_t *session, const uint8_t *payload, size_t size);
int rtmp_handle_connect(rtmp_session_t *session, const uint8_t *payload, size_t size);
int rtmp_handle_create_stream(rtmp_session_t *session, const uint8_t *payload, size_t size);
int rtmp_handle_publish(rtmp_session_t *session, const uint8_t *payload, size_t size);
int rtmp_handle_play(rtmp_session_t *session, const uint8_t *payload, size_t size);
int rtmp_handle_delete_stream(rtmp_session_t *session, const uint8_t *payload, size_t size);

// Control message functions
int rtmp_send_window_acknowledgement_size(rtmp_session_t *session, uint32_t size);
int rtmp_send_set_peer_bandwidth(rtmp_session_t *session, uint32_t size, uint8_t limit_type);
int rtmp_send_user_control(rtmp_session_t *session, uint16_t event_type, uint32_t event_data);
int rtmp_send_chunk_size(rtmp_session_t *session, uint32_t chunk_size);

#endif // RTMP_COMMANDS_H