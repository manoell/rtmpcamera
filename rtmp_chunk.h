#ifndef RTMP_CHUNK_H
#define RTMP_CHUNK_H

#include <stdint.h>
#include <stdbool.h>
#include "rtmp_protocol.h"

// Chunk size constants
#define RTMP_DEFAULT_CHUNK_SIZE 128
#define RTMP_MAX_CHUNK_SIZE 65536

// Chunk stream ID constants
#define RTMP_CHUNK_STREAM_PROTOCOL 2
#define RTMP_CHUNK_STREAM_COMMAND 3
#define RTMP_CHUNK_STREAM_METADATA 4
#define RTMP_CHUNK_STREAM_VIDEO 6
#define RTMP_CHUNK_STREAM_AUDIO 7

// Chunk header types
typedef enum {
    CHUNK_TYPE_0 = 0, // Full header (11 bytes)
    CHUNK_TYPE_1 = 1, // No message ID (7 bytes)
    CHUNK_TYPE_2 = 2, // Timestamp delta only (3 bytes)
    CHUNK_TYPE_3 = 3  // No header (0 bytes)
} RTMPChunkHeaderType;

// Chunk header structure
typedef struct {
    uint32_t timestamp;      // Timestamp of message
    uint32_t messageLength;  // Length of message
    uint8_t messageType;     // Type of message
    uint32_t messageStreamId;// Stream ID
} RTMPChunkHeader;

// Chunk context for maintaining state
typedef struct {
    RTMPChunkHeader prevHeader;
    uint32_t timestampDelta;
    uint8_t *buffer;
    uint32_t bufferSize;
    uint32_t bytesRead;
} RTMPChunkContext;

// Functions for chunk encoding
bool rtmp_chunk_write(RTMPContext *rtmp, RTMPPacket *packet);
bool rtmp_chunk_write_header(uint8_t *buf, size_t size, RTMPChunkHeader *header, 
                           RTMPChunkHeaderType type);
size_t rtmp_chunk_calculate_size(RTMPChunkHeader *header, RTMPChunkHeaderType type);

// Functions for chunk decoding
bool rtmp_chunk_read(RTMPContext *rtmp, RTMPPacket *packet);
bool rtmp_chunk_read_header(uint8_t *buf, size_t size, RTMPChunkHeader *header,
                          RTMPChunkHeaderType *type);
bool rtmp_chunk_read_basic_header(uint8_t *buf, uint8_t *fmt, uint32_t *csid);

// Chunk context management
RTMPChunkContext *rtmp_chunk_context_create(void);
void rtmp_chunk_context_destroy(RTMPChunkContext *ctx);
void rtmp_chunk_context_reset(RTMPChunkContext *ctx);

// Chunk size management
void rtmp_chunk_set_size(RTMPContext *rtmp, uint32_t size);
uint32_t rtmp_chunk_get_size(RTMPContext *rtmp);

#endif /* RTMP_CHUNK_H */