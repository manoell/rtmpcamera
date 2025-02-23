#ifndef RTMP_SERVER_INTEGRATION_H
#define RTMP_SERVER_INTEGRATION_H

#include "rtmp_protocol.h"

// Server states
typedef enum {
    RTMP_SERVER_STATE_STOPPED = 0,
    RTMP_SERVER_STATE_STARTING,
    RTMP_SERVER_STATE_RUNNING,
    RTMP_SERVER_STATE_ERROR
} RTMPServerState;

// Server configuration
typedef struct {
    uint16_t port;                      // Server port (default 1935)  
    uint32_t maxClients;                // Maximum concurrent clients
    uint32_t maxBandwidthPerClient;     // Maximum bandwidth per client in bps
    uint32_t chunkSize;                 // RTMP chunk size
    uint32_t windowSize;                // RTMP window size
    bool enableAuth;                    // Enable authentication
    char authKey[128];                  // Authentication key
    char allowedIPs[16][32];           // Allowed IP addresses
    uint32_t numAllowedIPs;            // Number of allowed IPs
} RTMPServerConfig;

// Server statistics
typedef struct {
    uint32_t currentClients;           // Current number of connected clients
    uint32_t totalClients;             // Total clients since start
    uint32_t bytesReceived;            // Total bytes received
    uint32_t bytesSent;                // Total bytes sent
    uint32_t droppedConnections;       // Number of dropped connections
    uint32_t failedAuths;              // Number of failed authentications
    uint32_t uptime;                   // Server uptime in seconds
} RTMPServerStats;

// Server context
typedef struct RTMPServerContext RTMPServerContext;

// Creation/destruction
RTMPServerContext *rtmp_server_create(void);
void rtmp_server_destroy(RTMPServerContext *ctx);

// Server control
bool rtmp_server_start(RTMPServerContext *ctx);
void rtmp_server_stop(RTMPServerContext *ctx);
bool rtmp_server_restart(RTMPServerContext *ctx);

// Configuration
void rtmp_server_set_config(RTMPServerContext *ctx, const RTMPServerConfig *config);
const RTMPServerConfig *rtmp_server_get_config(RTMPServerContext *ctx);

// Status and statistics
RTMPServerState rtmp_server_get_state(RTMPServerContext *ctx);
const RTMPServerStats *rtmp_server_get_stats(RTMPServerContext *ctx);
bool rtmp_server_is_running(RTMPServerContext *ctx);

// Client management
bool rtmp_server_disconnect_client(RTMPServerContext *ctx, const char *clientId);
bool rtmp_server_block_ip(RTMPServerContext *ctx, const char *ip);
bool rtmp_server_allow_ip(RTMPServerContext *ctx, const char *ip);

// Stream management
bool rtmp_server_list_streams(RTMPServerContext *ctx, char **streams, uint32_t *count);
bool rtmp_server_get_stream_info(RTMPServerContext *ctx, const char *streamName, RTMPStreamInfo *info);
bool rtmp_server_close_stream(RTMPServerContext *ctx, const char *streamName);

// Event callbacks
typedef void (*RTMPServerClientCallback)(RTMPServerContext *ctx, const char *clientId, bool connected, void *userData);
typedef void (*RTMPServerStreamCallback)(RTMPServerContext *ctx, const char *streamName, bool started, void *userData);
typedef void (*RTMPServerErrorCallback)(RTMPServerContext *ctx, RTMPError error, const char *description, void *userData);

void rtmp_server_set_client_callback(RTMPServerContext *ctx, RTMPServerClientCallback callback, void *userData);
void rtmp_server_set_stream_callback(RTMPServerContext *ctx, RTMPServerStreamCallback callback, void *userData);
void rtmp_server_set_error_callback(RTMPServerContext *ctx, RTMPServerErrorCallback callback, void *userData);

#endif /* RTMP_SERVER_INTEGRATION_H */