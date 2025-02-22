#ifndef RTMP_CORE_H
#define RTMP_CORE_H

#include <time.h>

// Error codes
#define RTMP_SUCCESS 0
#define RTMP_ERROR_INIT_FAILED -1
#define RTMP_ERROR_ALREADY_RUNNING -2
#define RTMP_ERROR_SOCKET_CREATE -3
#define RTMP_ERROR_SOCKET_BIND -4
#define RTMP_ERROR_SOCKET_LISTEN -5
#define RTMP_ERROR_THREAD_CREATE -6
#define RTMP_ERROR_INVALID_CONNECTION -7
#define RTMP_ERROR_STREAM_RESET -8

// Server configuration
typedef struct {
    int port;
    int max_connections;
    int buffer_size;
    int enable_recovery;
    int log_level;
} rtmp_config_t;

// Connection structure
typedef struct {
    int socket;
    int active;
    char *client_ip;
    int client_port;
    time_t last_heartbeat;
    int has_stream;
    void *stream_data;
    unsigned long bytes_received;
    unsigned long bytes_sent;
    void *user_data;
} rtmp_connection_t;

// Server statistics
typedef struct {
    time_t start_time;
    unsigned long uptime;
    unsigned long total_connections;
    unsigned int active_streams;
    unsigned long bytes_received;
    unsigned long bytes_sent;
    unsigned long dropped_frames;
    float avg_bandwidth;
} rtmp_stats_t;

// Core functions
int rtmp_core_init(void);
int rtmp_core_start(void);
int rtmp_core_stop(void);
const rtmp_stats_t* rtmp_core_get_stats(void);

// Connection management
rtmp_connection_t* rtmp_connection_create(int socket);
void rtmp_connection_destroy(rtmp_connection_t *conn);
int rtmp_connection_is_healthy(const rtmp_connection_t *conn);

// Logging functions
void rtmp_log_error(const char *message);
void rtmp_log_warning(const char *message);
void rtmp_log_info(const char *message);
void rtmp_log_debug(const char *message);

#endif // RTMP_CORE_H