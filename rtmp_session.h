#ifndef _RTMP_SESSION_H
#define _RTMP_SESSION_H

#include <stdint.h>
#include <stdbool.h>
#include "rtmp_protocol.h"

// Session states
typedef enum {
    RTMP_SESSION_STATE_NEW = 0,
    RTMP_SESSION_STATE_HANDSHAKING,
    RTMP_SESSION_STATE_CONNECTING,
    RTMP_SESSION_STATE_CONNECTED,
    RTMP_SESSION_STATE_PUBLISHING,
    RTMP_SESSION_STATE_PLAYING,
    RTMP_SESSION_STATE_CLOSING,
    RTMP_SESSION_STATE_CLOSED,
    RTMP_SESSION_STATE_ERROR
} rtmp_session_state_t;

// Session configuration
typedef struct {
    char app_name[128];
    char stream_name[128];
    char stream_key[128];
    bool is_publisher;
    uint32_t chunk_size;
    uint32_t ping_interval;
    uint32_t timeout_ms;
    char client_id[64];
    bool enable_audio;
    bool enable_video;
} rtmp_session_config_t;

// Session statistics
typedef struct {
    uint64_t bytes_received;
    uint64_t bytes_sent;
    uint32_t messages_received;
    uint32_t messages_sent;
    uint32_t connect_time_ms;
    uint32_t uptime_ms;
    float video_fps;
    float audio_bitrate;
    float video_bitrate;
    uint32_t dropped_frames;
    uint32_t last_ping_ms;
    uint32_t ping_rtts[10];
} rtmp_session_stats_t;

// Session events
typedef enum {
    RTMP_SESSION_EVENT_CONNECTED,
    RTMP_SESSION_EVENT_DISCONNECTED,
    RTMP_SESSION_EVENT_PUBLISH_START,
    RTMP_SESSION_EVENT_PUBLISH_STOP,
    RTMP_SESSION_EVENT_PLAY_START,
    RTMP_SESSION_EVENT_PLAY_STOP,
    RTMP_SESSION_EVENT_AUDIO_DATA,
    RTMP_SESSION_EVENT_VIDEO_DATA,
    RTMP_SESSION_EVENT_METADATA,
    RTMP_SESSION_EVENT_ERROR
} rtmp_session_event_t;

// Forward declaration
typedef struct rtmp_session_t rtmp_session_t;

// Session callbacks
typedef struct {
    void (*on_state_change)(rtmp_session_t *session, rtmp_session_state_t old_state, rtmp_session_state_t new_state);
    void (*on_event)(rtmp_session_t *session, rtmp_session_event_t event, const void *data, size_t data_len);
    void (*on_audio)(rtmp_session_t *session, const uint8_t *data, size_t len, uint32_t timestamp);
    void (*on_video)(rtmp_session_t *session, const uint8_t *data, size_t len, uint32_t timestamp);
    void (*on_metadata)(rtmp_session_t *session, const char *name, const uint8_t *data, size_t len);
    void (*on_error)(rtmp_session_t *session, const char *error);
} rtmp_session_callbacks_t;

// Session API
rtmp_session_t* rtmp_session_create(const rtmp_session_config_t *config, const rtmp_session_callbacks_t *callbacks);
void rtmp_session_destroy(rtmp_session_t *session);

// Session control
rtmp_error_t rtmp_session_start(rtmp_session_t *session);
rtmp_error_t rtmp_session_stop(rtmp_session_t *session);
rtmp_error_t rtmp_session_close(rtmp_session_t *session);

// Media control
rtmp_error_t rtmp_session_send_audio(rtmp_session_t *session, const uint8_t *data, size_t len, uint32_t timestamp);
rtmp_error_t rtmp_session_send_video(rtmp_session_t *session, const uint8_t *data, size_t len, uint32_t timestamp);
rtmp_error_t rtmp_session_send_metadata(rtmp_session_t *session, const char *name, const uint8_t *data, size_t len);

// Session management
rtmp_error_t rtmp_session_process(rtmp_session_t *session);
rtmp_error_t rtmp_session_handle_message(rtmp_session_t *session, rtmp_chunk_t *chunk);
rtmp_error_t rtmp_session_ping(rtmp_session_t *session);

// Status and info
rtmp_session_state_t rtmp_session_get_state(const rtmp_session_t *session);
const rtmp_session_stats_t* rtmp_session_get_stats(const rtmp_session_t *session);
const char* rtmp_session_get_error(const rtmp_session_t *session);
bool rtmp_session_is_connected(const rtmp_session_t *session);
bool rtmp_session_is_active(const rtmp_session_t *session);

// Utility functions
void rtmp_session_set_chunk_size(rtmp_session_t *session, uint32_t size);
void rtmp_session_set_buffer_time(rtmp_session_t *session, uint32_t ms);
void rtmp_session_set_stream_id(rtmp_session_t *session, uint32_t stream_id);

#endif /* _RTMP_SESSION_H */