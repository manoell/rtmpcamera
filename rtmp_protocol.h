#ifndef _RTMP_PROTOCOL_H
#define _RTMP_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "rtmp_chunk.h"

// Protocol constants
#define RTMP_VERSION                 3
#define RTMP_HANDSHAKE_SIZE         1536
#define RTMP_DEFAULT_PORT           1935
#define RTMP_DEFAULT_TIMEOUT        30000  // 30 seconds
#define RTMP_MAX_CHANNELS          65600

// Message types
#define RTMP_MSG_SET_CHUNK_SIZE     1
#define RTMP_MSG_ABORT              2
#define RTMP_MSG_ACKNOWLEDGEMENT    3
#define RTMP_MSG_USER_CONTROL       4
#define RTMP_MSG_WINDOW_ACK_SIZE    5
#define RTMP_MSG_SET_PEER_BW        6
#define RTMP_MSG_AUDIO              8
#define RTMP_MSG_VIDEO              9
#define RTMP_MSG_DATA_AMF3          15
#define RTMP_MSG_SHARED_OBJ_AMF3    16
#define RTMP_MSG_COMMAND_AMF3       17
#define RTMP_MSG_DATA_AMF0          18
#define RTMP_MSG_SHARED_OBJ_AMF0    19
#define RTMP_MSG_COMMAND_AMF0       20
#define RTMP_MSG_AGGREGATE          22

// User control message types
#define RTMP_USER_STREAM_BEGIN      0
#define RTMP_USER_STREAM_EOF        1
#define RTMP_USER_STREAM_DRY        2
#define RTMP_USER_SET_BUFFER_LEN    3
#define RTMP_USER_STREAM_IS_REC     4
#define RTMP_USER_PING_REQUEST      6
#define RTMP_USER_PING_RESPONSE     7

// Protocol states
typedef enum {
    RTMP_STATE_UNINITIALIZED = 0,
    RTMP_STATE_VERSION_SENT,
    RTMP_STATE_ACK_SENT,
    RTMP_STATE_HANDSHAKE_DONE,
    RTMP_STATE_CONNECT_PENDING,
    RTMP_STATE_CONNECTED,
    RTMP_STATE_DISCONNECTED,
    RTMP_STATE_ERROR
} rtmp_state_t;

// Error codes
typedef enum {
    RTMP_ERROR_OK = 0,
    RTMP_ERROR_INVALID_STATE = -1,
    RTMP_ERROR_SOCKET = -2,
    RTMP_ERROR_HANDSHAKE = -3,
    RTMP_ERROR_CONNECT = -4,
    RTMP_ERROR_STREAM = -5,
    RTMP_ERROR_CHUNK = -6,
    RTMP_ERROR_PROTOCOL = -7,
    RTMP_ERROR_MEMORY = -8,
    RTMP_ERROR_TIMEOUT = -9
} rtmp_error_t;

// Protocol configuration
typedef struct {
    uint32_t chunk_size;
    uint32_t window_size;
    uint32_t peer_bandwidth;
    uint8_t peer_bandwidth_limit_type;
    bool tcp_nodelay;
    uint32_t timeout_ms;
} rtmp_config_t;

// Protocol statistics
typedef struct {
    uint64_t bytes_in;
    uint64_t bytes_out;
    uint32_t messages_in;
    uint32_t messages_out;
    uint32_t chunks_in;
    uint32_t chunks_out;
    uint32_t handshake_time_ms;
    uint32_t connect_time_ms;
    float bandwidth_in;
    float bandwidth_out;
} rtmp_stats_t;

// Protocol context
typedef struct rtmp_context_t rtmp_context_t;

// Protocol callbacks
typedef struct {
    void (*on_state_change)(rtmp_context_t *ctx, rtmp_state_t old_state, rtmp_state_t new_state);
    void (*on_chunk_received)(rtmp_context_t *ctx, rtmp_chunk_t *chunk);
    void (*on_message_received)(rtmp_context_t *ctx, uint8_t type, uint32_t stream_id, const uint8_t *data, size_t len);
    void (*on_error)(rtmp_context_t *ctx, rtmp_error_t error, const char *message);
    void (*on_command)(rtmp_context_t *ctx, const char *command, const uint8_t *data, size_t len);
} rtmp_callbacks_t;

// Protocol functions
rtmp_context_t* rtmp_create(const rtmp_config_t *config, const rtmp_callbacks_t *callbacks);
void rtmp_destroy(rtmp_context_t *ctx);

// Connection management
rtmp_error_t rtmp_connect(rtmp_context_t *ctx, const char *host, uint16_t port);
rtmp_error_t rtmp_disconnect(rtmp_context_t *ctx);
bool rtmp_is_connected(const rtmp_context_t *ctx);

// Handshake
rtmp_error_t rtmp_handshake(rtmp_context_t *ctx);
bool rtmp_is_handshake_done(const rtmp_context_t *ctx);

// Message handling
rtmp_error_t rtmp_send_message(rtmp_context_t *ctx, uint8_t type, uint32_t stream_id, const uint8_t *data, size_t len);
rtmp_error_t rtmp_send_command(rtmp_context_t *ctx, const char *command, const uint8_t *data, size_t len);
rtmp_error_t rtmp_send_video(rtmp_context_t *ctx, const uint8_t *data, size_t len, uint32_t timestamp);
rtmp_error_t rtmp_send_audio(rtmp_context_t *ctx, const uint8_t *data, size_t len, uint32_t timestamp);

// Stream control
rtmp_error_t rtmp_create_stream(rtmp_context_t *ctx, uint32_t *stream_id);
rtmp_error_t rtmp_delete_stream(rtmp_context_t *ctx, uint32_t stream_id);
rtmp_error_t rtmp_publish(rtmp_context_t *ctx, uint32_t stream_id, const char *name, const char *type);
rtmp_error_t rtmp_play(rtmp_context_t *ctx, uint32_t stream_id, const char *name);
rtmp_error_t rtmp_pause(rtmp_context_t *ctx, uint32_t stream_id, bool pause);

// Flow control
rtmp_error_t rtmp_set_chunk_size(rtmp_context_t *ctx, uint32_t size);
rtmp_error_t rtmp_set_window_size(rtmp_context_t *ctx, uint32_t size);
rtmp_error_t rtmp_set_peer_bandwidth(rtmp_context_t *ctx, uint32_t size, uint8_t limit_type);

// Status and statistics
rtmp_state_t rtmp_get_state(const rtmp_context_t *ctx);
const rtmp_stats_t* rtmp_get_stats(const rtmp_context_t *ctx);
const char* rtmp_get_error_string(rtmp_error_t error);

// Utility functions
bool rtmp_is_valid_message_type(uint8_t type);
const char* rtmp_get_message_type_string(uint8_t type);

#endif /* _RTMP_PROTOCOL_H */