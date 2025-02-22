#include "rtmp_chunk.h"
#include "rtmp_diagnostics.h"
#include <string.h>
#include <arpa/inet.h>

// RTMP chunk types
#define RTMP_CHUNK_TYPE_0 0  // Full header
#define RTMP_CHUNK_TYPE_1 1  // Timestamp delta
#define RTMP_CHUNK_TYPE_2 2  // Timestamp delta only
#define RTMP_CHUNK_TYPE_3 3  // No header

// Maximum number of chunk streams
#define RTMP_MAX_CHUNK_STREAMS 64

// Chunk stream state
typedef struct {
    uint32_t timestamp;
    uint32_t message_length;
    uint8_t message_type;
    uint32_t stream_id;
    uint8_t *message_data;
    size_t bytes_received;
} ChunkStreamState;

// Chunk context
static struct {
    ChunkStreamState streams[RTMP_MAX_CHUNK_STREAMS];
    uint32_t chunk_size;
    bool initialized;
} chunk_context = {
    .chunk_size = RTMP_DEFAULT_CHUNK_SIZE,
    .initialized = false
};

void rtmp_chunk_init(void) {
    if (chunk_context.initialized) return;
    
    memset(&chunk_context, 0, sizeof(chunk_context));
    chunk_context.chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
    chunk_context.initialized = true;
}

void rtmp_chunk_set_size(uint32_t size) {
    if (size < 1 || size > 65536) return;
    chunk_context.chunk_size = size;
}

uint32_t rtmp_chunk_get_size(const RTMPPacket *packet) {
    if (!packet) return 0;
    
    // Calculate basic header size
    size_t header_size = 1;  // Basic header is at least 1 byte
    if (packet->m_nChannel >= 64) {
        header_size = 2;
    } else if (packet->m_nChannel >= 320) {
        header_size = 3;
    }
    
    // Add message header size based on chunk type
    switch (packet->m_headerType) {
        case RTMP_CHUNK_TYPE_0:
            header_size += 11;
            break;
        case RTMP_CHUNK_TYPE_1:
            header_size += 7;
            break;
        case RTMP_CHUNK_TYPE_2:
            header_size += 3;
            break;
        case RTMP_CHUNK_TYPE_3:
            break;
    }
    
    // Calculate number of chunks needed
    uint32_t chunks = (packet->m_nBodySize + chunk_context.chunk_size - 1) / chunk_context.chunk_size;
    
    // Total size is header plus chunked body
    return header_size + (chunks * chunk_context.chunk_size);
}

size_t rtmp_chunk_serialize(const RTMPPacket *packet, uint8_t *buffer, size_t buffer_size) {
    if (!packet || !buffer || buffer_size == 0) return 0;
    
    size_t offset = 0;
    
    // Write basic header
    if (packet->m_nChannel < 64) {
        buffer[offset++] = (packet->m_headerType << 6) | packet->m_nChannel;
    } else if (packet->m_nChannel < 320) {
        buffer[offset++] = (packet->m_headerType << 6) | 0;
        buffer[offset++] = packet->m_nChannel - 64;
    } else {
        buffer[offset++] = (packet->m_headerType << 6) | 1;
        uint16_t channel = htons(packet->m_nChannel - 64);
        memcpy(buffer + offset, &channel, 2);
        offset += 2;
    }
    
    // Write message header based on type
    if (packet->m_headerType == RTMP_CHUNK_TYPE_0) {
        // Timestamp
        uint32_t timestamp = htonl(packet->m_nTimeStamp);
        memcpy(buffer + offset, &timestamp, 4);
        offset += 4;
        
        // Message length
        uint32_t length = htonl(packet->m_nBodySize);
        memcpy(buffer + offset, &length, 3);
        offset += 3;
        
        // Message type
        buffer[offset++] = packet->m_packetType;
        
        // Stream ID
        uint32_t stream_id = htonl(packet->m_nInfoField2);
        memcpy(buffer + offset, &stream_id, 4);
        offset += 4;
    } else if (packet->m_headerType == RTMP_CHUNK_TYPE_1) {
        // Timestamp delta
        uint32_t timestamp = htonl(packet->m_nTimeStamp);
        memcpy(buffer + offset, &timestamp, 3);
        offset += 3;
        
        // Message length
        uint32_t length = htonl(packet->m_nBodySize);
        memcpy(buffer + offset, &length, 3);
        offset += 3;
        
        // Message type
        buffer[offset++] = packet->m_packetType;
    } else if (packet->m_headerType == RTMP_CHUNK_TYPE_2) {
        // Timestamp delta only
        uint32_t timestamp = htonl(packet->m_nTimeStamp);
        memcpy(buffer + offset, &timestamp, 3);
        offset += 3;
    }
    
    // Write packet body in chunks
    size_t remaining = packet->m_nBodySize;
    size_t body_offset = 0;
    
    while (remaining > 0) {
        size_t chunk_size = remaining > chunk_context.chunk_size ? 
                           chunk_context.chunk_size : remaining;
        
        if (offset + chunk_size > buffer_size) {
            rtmp_diagnostic_log("Buffer overflow while serializing chunk");
            return 0;
        }
        
        memcpy(buffer + offset, packet->m_body + body_offset, chunk_size);
        offset += chunk_size;
        body_offset += chunk_size;
        remaining -= chunk_size;
    }
    
    return offset;
}

RTMPPacket* rtmp_chunk_parse(const uint8_t *buffer, size_t size) {
    if (!buffer || size < 1) return NULL;
    
    RTMPPacket *packet = (RTMPPacket*)calloc(1, sizeof(RTMPPacket));
    if (!packet) return NULL;
    
    size_t offset = 0;
    
    // Parse basic header
    uint8_t basic_header = buffer[offset++];
    packet->m_headerType = (basic_header >> 6) & 0x03;
    uint32_t chunk_stream_id = basic_header & 0x3F;
    
    if (chunk_stream_id == 0) {
        if (size < offset + 1) {
            free(packet);
            return NULL;
        }
        chunk_stream_id = buffer[offset++] + 64;
    } else if (chunk_stream_id == 1) {
        if (size < offset + 2) {
            free(packet);
            return NULL;
        }
        chunk_stream_id = (buffer[offset] + 64) + (buffer[offset+1] << 8);
        offset += 2;
    }
    
    packet->m_nChannel = chunk_stream_id;
    ChunkStreamState *state = &chunk_context.streams[chunk_stream_id % RTMP_MAX_CHUNK_STREAMS];
    
    // Parse message header
    if (packet->m_headerType == RTMP_CHUNK_TYPE_0) {
        if (size < offset + 11) {
            free(packet);
            return NULL;
        }
        
        // Full header
        memcpy(&packet->m_nTimeStamp, buffer + offset, 4);
        packet->m_nTimeStamp = ntohl(packet->m_nTimeStamp);
        offset += 4;
        
        uint32_t message_length;
        memcpy(&message_length, buffer + offset, 3);
        message_length = ntohl(message_length >> 8);
        packet->m_nBodySize = message_length;
        offset += 3;
        
        packet->m_packetType = buffer[offset++];
        
        memcpy(&packet->m_nInfoField2, buffer + offset, 4);
        packet->m_nInfoField2 = ntohl(packet->m_nInfoField2);
        offset += 4;
        
        // Update state
        state->timestamp = packet->m_nTimeStamp;
        state->message_length = packet->m_nBodySize;
        state->message_type = packet->m_packetType;
        state->stream_id = packet->m_nInfoField2;
        
    } else if (packet->m_headerType == RTMP_CHUNK_TYPE_1) {
        if (size < offset + 7) {
            free(packet);
            return NULL;
        }
        
        // Delta + message type + stream ID
        uint32_t timestamp_delta;
        memcpy(&timestamp_delta, buffer + offset, 3);
        timestamp_delta = ntohl(timestamp_delta >> 8);
        packet->m_nTimeStamp = state->timestamp + timestamp_delta;
        offset += 3;
        
        uint32_t message_length;
        memcpy(&message_length, buffer + offset, 3);
        message_length = ntohl(message_length >> 8);
        packet->m_nBodySize = message_length;
        offset += 3;
        
        packet->m_packetType = buffer[offset++];
        packet->m_nInfoField2 = state->stream_id;
        
        // Update state
        state->timestamp = packet->m_nTimeStamp;
        state->message_length = packet->m_nBodySize;
        state->message_type = packet->m_packetType;
        
    } else if (packet->m_headerType == RTMP_CHUNK_TYPE_2) {
        if (size < offset + 3) {
            free(packet);
            return NULL;
        }
        
        // Delta only
        uint32_t timestamp_delta;
        memcpy(&timestamp_delta, buffer + offset, 3);
        timestamp_delta = ntohl(timestamp_delta >> 8);
        packet->m_nTimeStamp = state->timestamp + timestamp_delta;
        offset += 3;
        
        packet->m_nBodySize = state->message_length;
        packet->m_packetType = state->message_type;
        packet->m_nInfoField2 = state->stream_id;
        
        // Update state
        state->timestamp = packet->m_nTimeStamp;
        
    } else {
        // No header, use saved state
        packet->m_nTimeStamp = state->timestamp;
        packet->m_nBodySize = state->message_length;
        packet->m_packetType = state->message_type;
        packet->m_nInfoField2 = state->stream_id;
    }
    
    // Read packet body
    if (packet->m_nBodySize > 0) {
        packet->m_body = (uint8_t*)malloc(packet->m_nBodySize);
        if (!packet->m_body) {
            free(packet);
            return NULL;
        }
        
        size_t remaining = packet->m_nBodySize;
        size_t body_offset = 0;
        
        while (remaining > 0 && offset < size) {
            size_t chunk_size = remaining > chunk_context.chunk_size ? 
                               chunk_context.chunk_size : remaining;
            
            if (offset + chunk_size > size) {
                chunk_size = size - offset;
            }
            
            memcpy(packet->m_body + body_offset, buffer + offset, chunk_size);
            offset += chunk_size;
            body_offset += chunk_size;
            remaining -= chunk_size;
        }
        
        if (remaining > 0) {
            free(packet->m_body);
            free(packet);
            return NULL;
        }
    }
    
    return packet;
}

void rtmp_packet_free(RTMPPacket *packet) {
    if (!packet) return;
    free(packet->m_body);
    free(packet);
}

void rtmp_chunk_reset_stream(uint32_t chunk_stream_id) {
    if (chunk_stream_id >= RTMP_MAX_CHUNK_STREAMS) return;
    memset(&chunk_context.streams[chunk_stream_id], 0, sizeof(ChunkStreamState));
}

void rtmp_chunk_reset_all(void) {
    memset(chunk_context.streams, 0, sizeof(chunk_context.streams));
    chunk_context.chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
}