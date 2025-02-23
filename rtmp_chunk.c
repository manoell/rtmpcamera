#include "rtmp_chunk.h"
#include "rtmp_utils.h"
#include <stdlib.h>
#include <string.h>

// Maximum number of chunk streams we track
#define MAX_CHUNK_STREAMS 64

// Internal structures
typedef struct {
    RTMPChunkContext *chunks[MAX_CHUNK_STREAMS];
    uint32_t chunkSize;
} ChunkState;

// Helper functions
static RTMPChunkHeaderType get_chunk_type(RTMPChunkHeader *current, RTMPChunkHeader *previous);
static void update_chunk_context(RTMPChunkContext *ctx, RTMPChunkHeader *header);
static bool write_basic_header(uint8_t *buf, uint8_t fmt, uint32_t csid);
static size_t read_int24(uint8_t *buf);
static void write_int24(uint8_t *buf, uint32_t val);

// Chunk writing implementation
bool rtmp_chunk_write(RTMPContext *rtmp, RTMPPacket *packet) {
    if (!rtmp || !packet || !packet->data) return false;

    ChunkState *state = (ChunkState *)rtmp->userData;
    if (!state) return false;

    uint8_t header[16]; // Maximum possible header size
    RTMPChunkHeader chunkHeader = {
        .timestamp = packet->timestamp,
        .messageLength = packet->size,
        .messageType = packet->type,
        .messageStreamId = packet->streamId
    };

    // Get chunk context for this stream
    RTMPChunkContext *ctx = state->chunks[packet->type % MAX_CHUNK_STREAMS];
    if (!ctx) {
        ctx = rtmp_chunk_context_create();
        if (!ctx) return false;
        state->chunks[packet->type % MAX_CHUNK_STREAMS] = ctx;
    }

    // Determine chunk type
    RTMPChunkHeaderType type = get_chunk_type(&chunkHeader, &ctx->prevHeader);

    // Write chunk header
    size_t headerSize = rtmp_chunk_write_header(header, sizeof(header), &chunkHeader, type);
    if (headerSize == 0) return false;

    // Send header
    if (send(rtmp->socket, header, headerSize, 0) != headerSize) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to send chunk header");
        return false;
    }

    // Send payload in chunks
    uint32_t remaining = packet->size;
    uint8_t *data = packet->data;
    uint32_t chunkSize = state->chunkSize;

    while (remaining) {
        uint32_t size = remaining > chunkSize ? chunkSize : remaining;

        if (send(rtmp->socket, data, size, 0) != size) {
            rtmp_log(RTMP_LOG_ERROR, "Failed to send chunk payload");
            return false;
        }

        data += size;
        remaining -= size;

        // Write continuation header if needed
        if (remaining > 0) {
            uint8_t basicHeader = (packet->type & 0x3f) | (CHUNK_TYPE_3 << 6);
            if (send(rtmp->socket, &basicHeader, 1, 0) != 1) {
                rtmp_log(RTMP_LOG_ERROR, "Failed to send continuation header");
                return false;
            }
        }
    }

    // Update chunk context
    update_chunk_context(ctx, &chunkHeader);

    return true;
}

bool rtmp_chunk_write_header(uint8_t *buf, size_t size, RTMPChunkHeader *header,
                           RTMPChunkHeaderType type) {
    if (!buf || !header || size < 11) return false;

    size_t pos = 0;

    // Write basic header
    if (!write_basic_header(buf, type, header->messageStreamId)) return false;
    pos++;

    // Write message header based on type
    switch (type) {
        case CHUNK_TYPE_0:
            // Timestamp (3 bytes)
            write_int24(buf + pos, header->timestamp);
            pos += 3;
            // Message Length (3 bytes)
            write_int24(buf + pos, header->messageLength);
            pos += 3;
            // Message Type ID (1 byte)
            buf[pos++] = header->messageType;
            // Message Stream ID (4 bytes - little endian)
            buf[pos++] = header->messageStreamId & 0xff;
            buf[pos++] = (header->messageStreamId >> 8) & 0xff;
            buf[pos++] = (header->messageStreamId >> 16) & 0xff;
            buf[pos++] = (header->messageStreamId >> 24) & 0xff;
            break;

        case CHUNK_TYPE_1:
            // Timestamp Delta (3 bytes)
            write_int24(buf + pos, header->timestamp);
            pos += 3;
            // Message Length (3 bytes)
            write_int24(buf + pos, header->messageLength);
            pos += 3;
            // Message Type ID (1 byte)
            buf[pos++] = header->messageType;
            break;

        case CHUNK_TYPE_2:
            // Timestamp Delta (3 bytes)
            write_int24(buf + pos, header->timestamp);
            pos += 3;
            break;

        case CHUNK_TYPE_3:
            // No header
            break;
    }

    return pos;
}

// Chunk reading implementation
bool rtmp_chunk_read(RTMPContext *rtmp, RTMPPacket *packet) {
    if (!rtmp || !packet) return false;

    ChunkState *state = (ChunkState *)rtmp->userData;
    if (!state) return false;

    uint8_t basicHeader;
    if (recv(rtmp->socket, &basicHeader, 1, 0) != 1) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to read basic header");
        return false;
    }

    uint8_t fmt;
    uint32_t csid;
    if (!rtmp_chunk_read_basic_header(&basicHeader, &fmt, &csid)) {
        return false;
    }

    // Get or create chunk context
    RTMPChunkContext *ctx = state->chunks[csid % MAX_CHUNK_STREAMS];
    if (!ctx) {
        ctx = rtmp_chunk_context_create();
        if (!ctx) return false;
        state->chunks[csid % MAX_CHUNK_STREAMS] = ctx;
    }

    // Read chunk header
    RTMPChunkHeader header;
    RTMPChunkHeaderType type;
    uint8_t headerBuf[16];
    size_t headerSize = 0;

    switch (fmt) {
        case CHUNK_TYPE_0:
            headerSize = 11;
            break;
        case CHUNK_TYPE_1:
            headerSize = 7;
            break;
        case CHUNK_TYPE_2:
            headerSize = 3;
            break;
        case CHUNK_TYPE_3:
            headerSize = 0;
            break;
    }

    if (headerSize > 0) {
        if (recv(rtmp->socket, headerBuf, headerSize, 0) != headerSize) {
            rtmp_log(RTMP_LOG_ERROR, "Failed to read message header");
            return false;
        }

        if (!rtmp_chunk_read_header(headerBuf, headerSize, &header, &type)) {
            return false;
        }
    } else {
        // Copy from previous header for type 3
        header = ctx->prevHeader;
    }

    // Allocate packet data if needed
    if (!ctx->buffer) {
        ctx->buffer = (uint8_t *)malloc(header.messageLength);
        if (!ctx->buffer) {
            rtmp_log(RTMP_LOG_ERROR, "Failed to allocate chunk buffer");
            return false;
        }
        ctx->bufferSize = header.messageLength;
        ctx->bytesRead = 0;
    }

    // Read chunk data
    uint32_t chunkSize = state->chunkSize;
    uint32_t remaining = header.messageLength - ctx->bytesRead;
    uint32_t size = remaining > chunkSize ? chunkSize : remaining;

    if (recv(rtmp->socket, ctx->buffer + ctx->bytesRead, size, 0) != size) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to read chunk data");
        return false;
    }

    ctx->bytesRead += size;

    // If message complete, copy to packet
    if (ctx->bytesRead >= header.messageLength) {
        packet->data = (uint8_t *)malloc(header.messageLength);
        if (!packet->data) {
            rtmp_log(RTMP_LOG_ERROR, "Failed to allocate packet data");
            return false;
        }

        memcpy(packet->data, ctx->buffer, header.messageLength);
        packet->size = header.messageLength;
        packet->type = header.messageType;
        packet->timestamp = header.timestamp;
        packet->streamId = header.messageStreamId;

        // Reset chunk context
        ctx->bytesRead = 0;
        free(ctx->buffer);
        ctx->buffer = NULL;
        ctx->bufferSize = 0;
    }

    // Update chunk context
    update_chunk_context(ctx, &header);

    return true;
}

// Context management
RTMPChunkContext *rtmp_chunk_context_create(void) {
    RTMPChunkContext *ctx = (RTMPChunkContext *)calloc(1, sizeof(RTMPChunkContext));
    return ctx;
}

void rtmp_chunk_context_destroy(RTMPChunkContext *ctx) {
    if (!ctx) return;
    if (ctx->buffer) {
        free(ctx->buffer);
    }
    free(ctx);
}

void rtmp_chunk_context_reset(RTMPChunkContext *ctx) {
    if (!ctx) return;
    memset(&ctx->prevHeader, 0, sizeof(RTMPChunkHeader));
    ctx->timestampDelta = 0;
    if (ctx->buffer) {
        free(ctx->buffer);
        ctx->buffer = NULL;
    }
    ctx->bufferSize = 0;
    ctx->bytesRead = 0;
}

// Helper function implementations
static RTMPChunkHeaderType get_chunk_type(RTMPChunkHeader *current, RTMPChunkHeader *previous) {
    if (!previous->messageLength) {
        return CHUNK_TYPE_0;
    }

    if (current->messageStreamId != previous->messageStreamId) {
        return CHUNK_TYPE_0;
    }

    if (current->messageLength != previous->messageLength ||
        current->messageType != previous->messageType) {
        return CHUNK_TYPE_1;
    }

    if (current->timestamp != previous->timestamp) {
        return CHUNK_TYPE_2;
    }

    return CHUNK_TYPE_3;
}

static void update_chunk_context(RTMPChunkContext *ctx, RTMPChunkHeader *header) {
    if (!ctx || !header) return;
    
    ctx->timestampDelta = header->timestamp - ctx->prevHeader.timestamp;
    ctx->prevHeader = *header;
}

static bool write_basic_header(uint8_t *buf, uint8_t fmt, uint32_t csid) {
    if (csid >= 64) {
        if (csid >= 320) {
            buf[0] = (fmt << 6) | 1;
            buf[1] = (csid - 64) & 0xFF;
            buf[2] = ((csid - 64) >> 8) & 0xFF;
            return true;
        } else {
            buf[0] = (fmt << 6) | 0;
            buf[1] = (csid - 64) & 0xFF;
            return true;
        }
    } else {
        buf[0] = (fmt << 6) | csid;
        return true;
    }
}

static size_t read_int24(uint8_t *buf) {
    return (buf[0] << 16) | (buf[1] << 8) | buf[2];
}

static void write_int24(uint8_t *buf, uint32_t val) {
    buf[0] = (val >> 16) & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = val & 0xFF;
}

bool rtmp_chunk_read_basic_header(uint8_t *buf, uint8_t *fmt, uint32_t *csid) {
    if (!buf || !fmt || !csid) return false;

    *fmt = (buf[0] >> 6) & 0x03;
    *csid = buf[0] & 0x3F;

    if (*csid == 0) {
        *csid = buf[1] + 64;
        return true;
    } else if (*csid == 1) {
        *csid = (buf[2] << 8) + buf[1] + 64;
        return true;
    }

    return true;
}

bool rtmp_chunk_read_header(uint8_t *buf, size_t size, RTMPChunkHeader *header,
                          RTMPChunkHeaderType *type) {
    if (!buf || !header || !type || size < 3) return false;

    size_t pos = 0;

    // Parse header based on type
    switch (*type) {
        case CHUNK_TYPE_0:
            if (size < 11) return false;
            header->timestamp = read_int24(buf + pos);
            pos += 3;
            header->messageLength = read_int24(buf + pos);
            pos += 3;
            header->messageType = buf[pos++];
            header->messageStreamId = buf[pos] | (buf[pos+1] << 8) | 
                                    (buf[pos+2] << 16) | (buf[pos+3] << 24);
            break;

        case CHUNK_TYPE_1:
            if (size < 7) return false;
            header->timestamp = read_int24(buf + pos);
            pos += 3;
            header->messageLength = read_int24(buf + pos);
            pos += 3;
            header->messageType = buf[pos];
            break;

        case CHUNK_TYPE_2:
            if (size < 3) return false;
            header->timestamp = read_int24(buf);
            break;

        case CHUNK_TYPE_3:
            // No header to read
            break;
    }

    return true;
}

void rtmp_chunk_set_size(RTMPContext *rtmp, uint32_t size) {
    if (!rtmp) return;

    ChunkState *state = (ChunkState *)rtmp->userData;
    if (!state) return;

    if (size > RTMP_MAX_CHUNK_SIZE) {
        size = RTMP_MAX_CHUNK_SIZE;
    }

    state->chunkSize = size;
}

uint32_t rtmp_chunk_get_size(RTMPContext *rtmp) {
    if (!rtmp) return RTMP_DEFAULT_CHUNK_SIZE;

    ChunkState *state = (ChunkState *)rtmp->userData;
    if (!state) return RTMP_DEFAULT_CHUNK_SIZE;

    return state->chunkSize;
}