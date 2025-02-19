#include "rtmp_chunk.h"
#include "rtmp_log.h"
#include <stdlib.h>
#include <string.h>

RTMPChunk* rtmp_chunk_create(void) {
    RTMPChunk* chunk = calloc(1, sizeof(RTMPChunk));
    if (!chunk) {
        LOG_ERROR("Failed to allocate chunk");
        return NULL;
    }
    return chunk;
}

void rtmp_chunk_destroy(RTMPChunk* chunk) {
    if (chunk) {
        free(chunk->data);
        free(chunk);
    }
}

static int parse_basic_header(RTMPChunk* chunk, const uint8_t* data, size_t len, size_t* bytes_read) {
    if (len < 1) return -1;
    
    uint8_t basic_header = data[0];
    chunk->fmt = (basic_header >> 6) & 0x03;
    chunk->csid = basic_header & 0x3F;
    
    *bytes_read = 1;
    
    if (chunk->csid == 0) {
        if (len < 2) return -1;
        chunk->csid = data[1] + 64;
        *bytes_read = 2;
    } else if (chunk->csid == 1) {
        if (len < 3) return -1;
        chunk->csid = (data[2] << 8) + data[1] + 64;
        *bytes_read = 3;
    }
    
    return 0;
}

static int parse_message_header(RTMPChunk* chunk, const uint8_t* data, size_t len, size_t* bytes_read) {
    size_t pos = 0;
    
    switch (chunk->fmt) {
        case 0: // Type 0 - 11 bytes
            if (len < 11) return -1;
            
            chunk->timestamp = (data[pos] << 16) | (data[pos+1] << 8) | data[pos+2];
            pos += 3;
            
            chunk->length = (data[pos] << 16) | (data[pos+1] << 8) | data[pos+2];
            pos += 3;
            
            chunk->type = data[pos];
            pos += 1;
            
            chunk->stream_id = (data[pos] << 24) | (data[pos+1] << 16) | 
                             (data[pos+2] << 8) | data[pos+3];
            pos += 4;
            break;
            
        case 1: // Type 1 - 7 bytes
            if (len < 7) return -1;
            
            chunk->timestamp_delta = (data[pos] << 16) | (data[pos+1] << 8) | data[pos+2];
            pos += 3;
            
            chunk->length = (data[pos] << 16) | (data[pos+1] << 8) | data[pos+2];
            pos += 3;
            
            chunk->type = data[pos];
            pos += 1;
            break;
            
        case 2: // Type 2 - 3 bytes
            if (len < 3) return -1;
            
            chunk->timestamp_delta = (data[pos] << 16) | (data[pos+1] << 8) | data[pos+2];
            pos += 3;
            break;
            
        case 3: // Type 3 - 0 bytes
            // No header
            break;
    }
    
    *bytes_read = pos;
    return 0;
}

int rtmp_chunk_parse(RTMPChunk* chunk, const uint8_t* data, size_t len, size_t* bytes_read) {
    size_t header_size = 0;
    size_t pos = 0;
    
    // Parse basic header
    if (parse_basic_header(chunk, data, len, &header_size) < 0) {
        LOG_ERROR("Failed to parse basic header");
        return -1;
    }
    pos += header_size;
    
    // Parse message header
    if (parse_message_header(chunk, data + pos, len - pos, &header_size) < 0) {
        LOG_ERROR("Failed to parse message header");
        return -1;
    }
    pos += header_size;
    
    *bytes_read = pos;
    
    // Allocate data buffer if needed
    if (chunk->fmt <= 1) {
        free(chunk->data);
        chunk->data = malloc(chunk->length);
        if (!chunk->data) {
            LOG_ERROR("Failed to allocate chunk data buffer");
            return -1;
        }
        chunk->data_size = chunk->length;
        chunk->received = 0;
    }
    
    return 0;
}

int rtmp_chunk_serialize(const RTMPChunk* chunk, uint8_t* buffer, size_t len, size_t* bytes_written) {
    if (!chunk || !buffer || !bytes_written) return -1;
    
    size_t pos = 0;
    
    // Basic header
    if (chunk->csid < 64) {
        if (len < 1) return -1;
        buffer[pos++] = (chunk->fmt << 6) | chunk->csid;
    } else if (chunk->csid < 320) {
        if (len < 2) return -1;
        buffer[pos++] = (chunk->fmt << 6);
        buffer[pos++] = chunk->csid - 64;
    } else {
        if (len < 3) return -1;
        buffer[pos++] = (chunk->fmt << 6) | 1;
        buffer[pos++] = (chunk->csid - 64) & 0xFF;
        buffer[pos++] = (chunk->csid - 64) >> 8;
    }
    
    // Message header
    switch (chunk->fmt) {
        case 0:
            if (len < pos + 11) return -1;
            buffer[pos++] = (chunk->timestamp >> 16) & 0xFF;
            buffer[pos++] = (chunk->timestamp >> 8) & 0xFF;
            buffer[pos++] = chunk->timestamp & 0xFF;
            buffer[pos++] = (chunk->length >> 16) & 0xFF;
            buffer[pos++] = (chunk->length >> 8) & 0xFF;
            buffer[pos++] = chunk->length & 0xFF;
            buffer[pos++] = chunk->type;
            buffer[pos++] = (chunk->stream_id >> 24) & 0xFF;
            buffer[pos++] = (chunk->stream_id >> 16) & 0xFF;
            buffer[pos++] = (chunk->stream_id >> 8) & 0xFF;
            buffer[pos++] = chunk->stream_id & 0xFF;
            break;
            
        case 1:
            if (len < pos + 7) return -1;
            buffer[pos++] = (chunk->timestamp_delta >> 16) & 0xFF;
            buffer[pos++] = (chunk->timestamp_delta >> 8) & 0xFF;
            buffer[pos++] = chunk->timestamp_delta & 0xFF;
            buffer[pos++] = (chunk->length >> 16) & 0xFF;
            buffer[pos++] = (chunk->length >> 8) & 0xFF;
            buffer[pos++] = chunk->length & 0xFF;
            buffer[pos++] = chunk->type;
            break;
            
        case 2:
            if (len < pos + 3) return -1;
            buffer[pos++] = (chunk->timestamp_delta >> 16) & 0xFF;
            buffer[pos++] = (chunk->timestamp_delta >> 8) & 0xFF;
            buffer[pos++] = chunk->timestamp_delta & 0xFF;
            break;
    }
    
    *bytes_written = pos;
    return 0;
}