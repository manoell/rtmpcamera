// rtmp_server_integration.h
#ifndef RTMP_SERVER_INTEGRATION_H
#define RTMP_SERVER_INTEGRATION_H

#include <stdbool.h>
#include <stdint.h>
#include "rtmp_core.h"
#include "rtmp_utils.h"
#include "rtmp_stream.h"
#include "rtmp_protocol.h"

// Server configurations
#define RTMP_DEFAULT_PORT 1935
#define RTMP_MAX_CONNECTIONS 10
#define RTMP_BUFFER_SIZE 131072
#define RTMP_TIMEOUT_SEC 30

// Server states 
typedef enum {
    RTMP_SERVER_STATE_STOPPED = 0,
    RTMP_SERVER_STATE_STARTING,
    RTMP_SERVER_STATE_RUNNING,
    RTMP_SERVER_STATE_ERROR,
    RTMP_SERVER_STATE_RESTARTING
} rtmp_server_state_t;

// Connection states
typedef enum {
    RTMP_CONN_STATE_NEW = 0,
    RTMP_CONN_STATE_HANDSHAKE,
    RTMP_CONN_STATE_CONNECT,
    RTMP_CONN_STATE_CREATE_STREAM,
    RTMP_CONN_STATE_PLAY,
    RTMP_CONN_STATE_PUBLISHING,
    RTMP_CONN_STATE_CLOSED
} rtmp_connection_state_t;

// Stream metadata struct
typedef struct {
    char app_name[128];
    char stream_name[128];
    uint32_t width;
    uint32_t height; 
    uint32_t frame_rate;
    uint32_t video_bitrate;
    uint32_t audio_bitrate;
    uint32_t audio_sample_rate;
    uint32_t audio_channels;
    bool has_video;
    bool has_audio;
    uint64_t bytes_in;
    uint64_t bytes_out;
    uint32_t dropped_frames;
    struct timeval connect_time;
    struct timeval publish_time;
} rtmp_stream_metadata_t;

// Connection struct
typedef struct rtmp_connection {
    int socket;
    rtmp_connection_state_t state;
    rtmp_stream_metadata_t metadata;
    rtmp_chunk_stream_t* chunk_stream;
    void* handshake_data;
    struct timeval last_recv_time;
    struct timeval last_send_time;
    uint32_t bytes_received;
    uint32_t bytes_sent;
    bool is_publisher;
    void* userdata;
    struct rtmp_connection* next;
} rtmp_connection_t;

// Server context struct
typedef struct {
    int listen_socket;
    uint16_t port;
    rtmp_server_state_t state;
    rtmp_connection_t* connections;
    uint32_t num_connections;
    pthread_t accept_thread;
    pthread_t monitor_thread;
    bool running;
    void* userdata;
    pthread_mutex_t lock;
} rtmp_server_context_t;

// Callback function prototypes
typedef void (*rtmp_connection_callback_t)(rtmp_connection_t* conn, void* userdata);
typedef void (*rtmp_metadata_callback_t)(rtmp_stream_metadata_t* metadata, void* userdata);
typedef void (*rtmp_frame_callback_t)(uint8_t* data, size_t size, uint32_t timestamp, bool is_keyframe, void* userdata);
typedef void (*rtmp_server_state_callback_t)(rtmp_server_state_t state, void* userdata);

// Server API functions
bool rtmp_server_initialize(void);
void rtmp_server_cleanup(void);
bool rtmp_server_start(uint16_t port);
void rtmp_server_stop(void);
rtmp_server_state_t rtmp_server_get_state(void);

// Connection management
rtmp_connection_t* rtmp_server_get_connection(int socket);
void rtmp_server_close_connection(rtmp_connection_t* conn);
uint32_t rtmp_server_get_num_connections(void);
bool rtmp_server_is_publishing(const char* stream_name);

// Callback registration
void rtmp_server_set_connection_callback(rtmp_connection_callback_t callback, void* userdata);
void rtmp_server_set_metadata_callback(rtmp_metadata_callback_t callback, void* userdata);
void rtmp_server_set_frame_callback(rtmp_frame_callback_t callback, void* userdata);
void rtmp_server_set_state_callback(rtmp_server_state_callback_t callback, void* userdata);

// Stream info and stats
bool rtmp_server_get_stream_info(const char* stream_name, rtmp_stream_metadata_t* info);
uint64_t rtmp_server_get_bytes_received(void);
uint64_t rtmp_server_get_bytes_sent(void);
uint32_t rtmp_server_get_dropped_frames(void);

// Configuration
void rtmp_server_set_chunk_size(uint32_t size);
void rtmp_server_set_window_ack_size(uint32_t size);
void rtmp_server_set_peer_bandwidth(uint32_t window_size, uint8_t limit_type);

// Diagnostic functions
const char* rtmp_server_state_string(rtmp_server_state_t state);
const char* rtmp_connection_state_string(rtmp_connection_state_t state);
void rtmp_server_dump_stats(void);

#endif /* RTMP_SERVER_INTEGRATION_H */