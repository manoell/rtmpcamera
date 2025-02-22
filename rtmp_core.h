#ifndef RTMP_CORE_H
#define RTMP_CORE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "rtmp_stream.h"

// Forward declarations
typedef struct RTMPContext RTMPContext;
typedef struct RTMPPacket RTMPPacket;

// Default RTMP chunk size
#define RTMP_DEFAULT_CHUNK_SIZE 128
#define RTMP_MAX_HEADER_SIZE 18

// Event types for core callbacks
typedef enum {
    RTMP_CORE_EVENT_CONNECTED,
    RTMP_CORE_EVENT_DISCONNECTED,
    RTMP_CORE_EVENT_PACKET,
    RTMP_CORE_EVENT_ERROR
} RTMPCoreEventType;

// Statistics structure
typedef struct {
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint32_t messages_sent;
    uint32_t messages_received;
} RTMPCoreStats;

// Event structure for callbacks
typedef struct {
    RTMPCoreEventType type;
    RTMPPacket *packet;        // Valid for RTMP_CORE_EVENT_PACKET
    const char *error_message; // Valid for RTMP_CORE_EVENT_ERROR
} RTMPCoreEvent;

// Callback function type
typedef void (*rtmp_core_callback_t)(const RTMPCoreEvent *event, void *context);

// Core RTMP functions
RTMPContext* rtmp_core_create(void);
void rtmp_core_destroy(RTMPContext *ctx);

// Connection management
bool rtmp_core_connect(RTMPContext *ctx, const char *url);
void rtmp_core_disconnect(RTMPContext *ctx);
bool rtmp_core_is_connected(RTMPContext *ctx);

// Stream management
bool rtmp_core_add_stream(RTMPContext *ctx, RTMPStream *stream);
void rtmp_core_remove_stream(RTMPContext *ctx, RTMPStream *stream);

// Callback and statistics
void rtmp_core_set_callback(RTMPContext *ctx, rtmp_core_callback_t callback, void *context);
void rtmp_core_get_stats(RTMPContext *ctx, RTMPCoreStats *stats);

#endif // RTMP_CORE_H