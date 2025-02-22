#ifndef RTMP_SESSION_H
#define RTMP_SESSION_H

#include <stdint.h>
#include <stddef.h>
#include "rtmp_core.h"

// Session states
#define RTMP_SESSION_STATE_INIT       0
#define RTMP_SESSION_STATE_HANDSHAKE  1
#define RTMP_SESSION_STATE_CONNECT    2
#define RTMP_SESSION_STATE_READY      3
#define RTMP_SESSION_STATE_CLOSING    4
#define RTMP_SESSION_STATE_CLOSED     5

// Stream states
#define RTMP_STREAM_STATE_IDLE        0
#define RTMP_STREAM_STATE_PUBLISHING  1
#define RTMP_STREAM_STATE_PLAYING     2

// Chunk stream structure
struct rtmp_chunk_stream_s {
    uint32_t timestamp;
    uint32_t timestamp_delta;
    uint32_t msg_length;
    uint8_t msg_type_id;
    uint32_t msg_stream_id;
    uint8_t *msg_data;
    size_t msg_data_pos;
};

// Session functions
rtmp_session_t* rtmp_session_create(int socket_fd);
void rtmp_session_destroy(rtmp_session_t *session);
int rtmp_session_send_data(rtmp_session_t *session, const uint8_t *data, size_t size);
int rtmp_session_close(rtmp_session_t *session);

// Stream management
uint32_t rtmp_session_create_stream(rtmp_session_t *session);
int rtmp_session_delete_stream(rtmp_session_t *session, uint32_t stream_id);
int rtmp_session_set_publish_stream(rtmp_session_t *session, const char *stream_name);
int rtmp_session_set_play_stream(rtmp_session_t *session, const char *stream_name);

// Chunk stream management
rtmp_chunk_stream_t* rtmp_get_chunk_stream(rtmp_session_t *session, uint32_t chunk_stream_id);
void rtmp_free_chunk_stream(rtmp_chunk_stream_t *chunk_stream);

// Buffer management
int rtmp_session_handle_acknowledgement(rtmp_session_t *session);
int rtmp_session_update_bytes_received(rtmp_session_t *session, size_t bytes);

// Media handling
int rtmp_session_send_video(rtmp_session_t *session, const uint8_t *data, size_t size, uint32_t timestamp);
int rtmp_session_send_audio(rtmp_session_t *session, const uint8_t *data, size_t size, uint32_t timestamp);
int rtmp_session_send_metadata(rtmp_session_t *session, const uint8_t *data, size_t size);

// State management
int rtmp_session_set_state(rtmp_session_t *session, int state);
int rtmp_session_get_state(rtmp_session_t *session);

#endif // RTMP_SESSION_H