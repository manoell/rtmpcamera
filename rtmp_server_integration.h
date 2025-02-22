#ifndef RTMP_SERVER_INTEGRATION_H
#define RTMP_SERVER_INTEGRATION_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// Forward declarations
typedef struct RTMPServerContext RTMPServerContext;
typedef struct RTMPPacket RTMPPacket;

// Event types for server callbacks
typedef enum {
    RTMP_SERVER_EVENT_STARTED,
    RTMP_SERVER_EVENT_CLIENT_CONNECTED,
    RTMP_SERVER_EVENT_CLIENT_DISCONNECTED,
    RTMP_SERVER_EVENT_STREAM_STARTED,
    RTMP_SERVER_EVENT_STREAM_ENDED,
    RTMP_SERVER_EVENT_ERROR
} RTMPServerEventType;

// Event structure for callbacks
typedef struct {
    RTMPServerEventType type;
    const char *error_message;  // Only valid for RTMP_SERVER_EVENT_ERROR
} RTMPServerEvent;

// Callback function type
typedef void (*rtmp_server_callback_t)(const RTMPServerEvent *event, void *context);

// Server management functions
RTMPServerContext* rtmp_server_create(void);
void rtmp_server_destroy(RTMPServerContext *server);

// Server configuration and control
bool rtmp_server_configure(RTMPServerContext *server, const char *port, const char *stream_path);
bool rtmp_server_start(RTMPServerContext *server);
bool rtmp_server_is_running(RTMPServerContext *server);

// Callback management
void rtmp_server_set_callback(RTMPServerContext *server, rtmp_server_callback_t callback, void *context);

// Statistics
uint32_t rtmp_server_get_client_count(RTMPServerContext *server);

#endif // RTMP_SERVER_INTEGRATION_H