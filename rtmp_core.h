#ifndef RTMP_CORE_H
#define RTMP_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>

// Forward declarations
typedef struct rtmp_server_s rtmp_server_t;
typedef struct rtmp_session_s rtmp_session_t;
typedef struct rtmp_chunk_stream_s rtmp_chunk_stream_t;
typedef struct rtmp_chunk_s rtmp_chunk_t;

// Callback types
typedef void (*rtmp_audio_callback)(rtmp_session_t *session, const uint8_t *data, size_t size, uint32_t timestamp);
typedef void (*rtmp_video_callback)(rtmp_session_t *session, const uint8_t *data, size_t size, uint32_t timestamp);
typedef void (*rtmp_metadata_callback)(rtmp_session_t *session, const uint8_t *data, size_t size);
typedef void (*rtmp_client_callback)(rtmp_server_t *server, rtmp_session_t *session);
typedef void (*rtmp_stream_callback)(rtmp_server_t *server, rtmp_session_t *session, const char *stream_name);

// Configuration structure
typedef struct {
    uint16_t port;
    uint32_t chunk_size;
    uint32_t window_size;
    uint32_t peer_bandwidth;
} rtmp_server_config_t;

#define RTMP_MAX_CHUNK_STREAMS 128
#define RTMP_MAX_CONNECTIONS 10
#define RTMP_DEFAULT_CHUNK_SIZE 128
#define RTMP_DEFAULT_WINDOW_SIZE 2500000

// Session structure
struct rtmp_session_s {
    int socket_fd;
    rtmp_server_t *server;
    uint32_t stream_id;
    
    // Chunk handling
    uint32_t in_chunk_size;
    uint32_t out_chunk_size;
    rtmp_chunk_stream_t *chunk_streams[RTMP_MAX_CHUNK_STREAMS];
    
    // Buffer management
    uint8_t *aac_sequence_header;
    size_t aac_sequence_header_size;
    uint8_t *avc_sequence_header;
    size_t avc_sequence_header_size;
    
    // Connection state
    uint32_t window_ack_size;
    uint32_t peer_bandwidth;
    uint8_t peer_bandwidth_limit_type;
    uint32_t last_ack_received;
    uint32_t bytes_received;
    int state;
    
    // Callbacks
    rtmp_audio_callback audio_callback;
    rtmp_video_callback video_callback;
    rtmp_metadata_callback metadata_callback;
    
    // Stream info
    char *stream_name;
    int is_publishing;
};

// Server structure
struct rtmp_server_s {
    int socket_fd;
    uint16_t port;
    int running;
    pthread_t accept_thread;
    
    // Configuration
    uint32_t chunk_size;
    uint32_t window_size;
    uint32_t peer_bandwidth;
    
    // Connection management
    rtmp_session_t *connections[RTMP_MAX_CONNECTIONS];
    int num_connections;
    pthread_mutex_t connections_mutex;
    
    // Callbacks
    rtmp_client_callback on_client_connect;
    rtmp_client_callback on_client_disconnect;
    rtmp_stream_callback on_publish_stream;
    rtmp_stream_callback on_play_stream;
};

// Server functions
rtmp_server_t* rtmp_server_create(void);
int rtmp_server_configure(rtmp_server_t *server, const rtmp_server_config_t *config);
void rtmp_server_set_callbacks(rtmp_server_t *server,
                             rtmp_client_callback on_connect,
                             rtmp_client_callback on_disconnect,
                             rtmp_stream_callback on_publish,
                             rtmp_stream_callback on_play);
int rtmp_server_start(rtmp_server_t *server);
int rtmp_server_stop(rtmp_server_t *server);
void rtmp_server_destroy(rtmp_server_t *server);

// Session functions
rtmp_session_t* rtmp_session_create(int socket_fd);
void rtmp_session_destroy(rtmp_session_t *session);
int rtmp_session_set_chunk_size(rtmp_session_t *session, uint32_t chunk_size);
int rtmp_session_process_input(rtmp_session_t *session, const uint8_t *data, size_t size);

// Utility functions
void rtmp_set_error(const char *fmt, ...);
const char* rtmp_get_error(void);

#endif // RTMP_CORE_H