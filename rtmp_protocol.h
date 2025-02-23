#ifndef RTMP_PROTOCOL_H
#define RTMP_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "rtmp_utils.h"

// Protocol constants
#define RTMP_VERSION           3
#define RTMP_HANDSHAKE_SIZE   1536
#define RTMP_CHUNK_SIZE       128
#define RTMP_DEFAULT_PORT     1935

// Message types
typedef enum {
    RTMP_MSG_CHUNK_SIZE = 1,
    RTMP_MSG_ABORT = 2,
    RTMP_MSG_ACK = 3,
    RTMP_MSG_USER_CONTROL = 4,
    RTMP_MSG_WINDOW_ACK_SIZE = 5,
    RTMP_MSG_SET_PEER_BW = 6,
    RTMP_MSG_AUDIO = 8,
    RTMP_MSG_VIDEO = 9,
    RTMP_MSG_DATA_AMF3 = 15,
    RTMP_MSG_SHARED_OBJ_AMF3 = 16,
    RTMP_MSG_COMMAND_AMF3 = 17,
    RTMP_MSG_DATA_AMF0 = 18,
    RTMP_MSG_SHARED_OBJ_AMF0 = 19,
    RTMP_MSG_COMMAND_AMF0 = 20,
    RTMP_MSG_AGGREGATE = 22
} RTMPMessageType;

// Chunk stream types
typedef enum {
    RTMP_CHUNK_TYPE_0 = 0, // Full header
    RTMP_CHUNK_TYPE_1 = 1, // Timestamp delta
    RTMP_CHUNK_TYPE_2 = 2, // Timestamp delta only
    RTMP_CHUNK_TYPE_3 = 3  // No header
} RTMPChunkType;

// RTMP connection states
typedef enum {
    RTMP_STATE_DISCONNECTED = 0,
    RTMP_STATE_HANDSHAKE_INIT,
    RTMP_STATE_HANDSHAKE_ACK,
    RTMP_STATE_CONNECT,
    RTMP_STATE_CREATE_STREAM,
    RTMP_STATE_PUBLISH,
    RTMP_STATE_PLAY,
    RTMP_STATE_CONNECTED
} RTMPState;

// RTMP connection settings
typedef struct {
    char app[128];
    char tcUrl[256];
    char swfUrl[256];
    char pageUrl[256];
    char streamName[128];
    bool publish;
    uint32_t windowAckSize;
    uint32_t peerBandwidth;
    uint8_t limitType;
} RTMPSettings;

// RTMP connection context
typedef struct RTMPContext {
    int socket;
    RTMPState state;
    RTMPSettings settings;
    uint32_t chunkSize;
    uint32_t streamId;
    uint32_t numInvokes;
    uint32_t windowAckSize;
    uint32_t bytesReceived;
    uint32_t lastAckSize;
    uint8_t *handshakeBuffer;
    void *userData;
    
    // Callbacks
    void (*onStateChange)(struct RTMPContext *ctx, RTMPState state);
    void (*onError)(struct RTMPContext *ctx, RTMPError error);
    void (*onPacket)(struct RTMPContext *ctx, RTMPPacket *packet);
} RTMPContext;

// Core protocol functions
RTMPContext *rtmp_create(void);
void rtmp_destroy(RTMPContext *ctx);
bool rtmp_connect(RTMPContext *ctx, const char *host, int port);
void rtmp_disconnect(RTMPContext *ctx);
bool rtmp_is_connected(RTMPContext *ctx);

// Message handling
bool rtmp_send_packet(RTMPContext *ctx, RTMPPacket *packet);
bool rtmp_read_packet(RTMPContext *ctx, RTMPPacket *packet);
void rtmp_handle_packet(RTMPContext *ctx, RTMPPacket *packet);

// Command functions
bool rtmp_send_connect(RTMPContext *ctx);
bool rtmp_send_create_stream(RTMPContext *ctx);
bool rtmp_send_publish(RTMPContext *ctx);
bool rtmp_send_play(RTMPContext *ctx);
bool rtmp_send_pause(RTMPContext *ctx, bool pause);
bool rtmp_send_seek(RTMPContext *ctx, uint32_t ms);

// Control messages
bool rtmp_send_chunk_size(RTMPContext *ctx, uint32_t size);
bool rtmp_send_ack(RTMPContext *ctx, uint32_t size);
bool rtmp_send_window_ack_size(RTMPContext *ctx, uint32_t size);
bool rtmp_send_set_peer_bandwidth(RTMPContext *ctx, uint32_t size, uint8_t type);

// Media functions
bool rtmp_send_audio(RTMPContext *ctx, const uint8_t *data, size_t size, uint32_t timestamp);
bool rtmp_send_video(RTMPContext *ctx, const uint8_t *data, size_t size, uint32_t timestamp);
bool rtmp_send_metadata(RTMPContext *ctx, const char *name, const AMFObject *obj);

// Utility functions
void rtmp_set_chunk_size(RTMPContext *ctx, uint32_t size);
void rtmp_set_window_ack_size(RTMPContext *ctx, uint32_t size);
void rtmp_set_stream_id(RTMPContext *ctx, uint32_t streamId);

#endif /* RTMP_PROTOCOL_H */