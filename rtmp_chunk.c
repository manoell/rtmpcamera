#include "rtmp_chunk.h"
#include "rtmp_log.h"
#include "rtmp_util.h"
#include <stdlib.h>
#include <string.h>

RTMPChunkStream* rtmp_chunk_stream_create(void) {
    RTMPChunkStream* cs = calloc(1, sizeof(RTMPChunkStream));
    if (!cs) {
        LOG_ERROR("Failed to allocate chunk stream");
        return NULL;
    }
    
    cs->chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
    cs->in_chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
    cs->out_chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
    cs->ack_window = 2500000;
    
    LOG_DEBUG("Created chunk stream with size %u", cs->chunk_size);
    return cs;
}

void rtmp_chunk_stream_destroy(RTMPChunkStream* cs) {
    if (!cs) return;
    
    for (int i = 0; i < RTMP_MAX_CHUNK_STREAMS; i++) {
        if (cs->chunks[i]) {
            rtmp_chunk_destroy(cs->chunks[i]);
        }
    }
    
    free(cs);
    LOG_DEBUG("Destroyed chunk stream");
}

RTMPChunk* rtmp_chunk_create(void) {
    RTMPChunk* chunk = calloc(1, sizeof(RTMPChunk));
    if (!chunk) {
        LOG_ERROR("Failed to allocate chunk");
        return NULL;
    }
    return chunk;
}

void rtmp_chunk_destroy(RTMPChunk* chunk) {
    if (!chunk) return;
    buffer_free(chunk->data);
    free(chunk);
}

void rtmp_chunk_reset(RTMPChunk* chunk) {
    if (!chunk) return;
    
    chunk->timestamp = 0;
    chunk->timestamp_delta = 0;
    chunk->length = 0;
    chunk->bytes_read = 0;
    chunk->extended_timestamp = 0;
    
    buffer_free(chunk->data);
    chunk->data = NULL;
    chunk->data_size = 0;
}

static int read_basic_header(const uint8_t* data, size_t len, RTMPChunk* chunk, size_t* bytes_read) {
    if (len < 1) return -1;
    
    uint8_t first_byte = data[0];
    chunk->fmt = (first_byte >> 6) & 0x03;
    chunk->csid = first_byte & 0x3F;
    *bytes_read = 1;
    
    if (chunk->csid == 0) {
        // 2 byte form
        if (len < 2) return -1;
        chunk->csid = data[1] + 64;
        *bytes_read = 2;
    } else if (chunk->csid == 1) {
        // 3 byte form
        if (len < 3) return -1;
        chunk->csid = (data[2] << 8) | data[1] + 64;
        *bytes_read = 3;
    }
    
    LOG_DEBUG("Read basic header: fmt=%d, csid=%u", chunk->fmt, chunk->csid);
    return 0;
}

static int read_message_header(RTMPChunkStream* cs, const uint8_t* data, size_t len, RTMPChunk* chunk, size_t* bytes_read) {
    size_t header_size = 0;
    *bytes_read = 0;
    
    RTMPChunk* prev_chunk = cs->chunks[chunk->csid];
    
    switch (chunk->fmt) {
        case 0: // Type 0 - 11 bytes
            if (len < 11) return -1;
            
            chunk->timestamp = read_uint24(data);
            chunk->length = read_uint24(data + 3);
            chunk->type = data[6];
            chunk->stream_id = read_uint32(data + 7);
            
            header_size = 11;
            
            if (chunk->timestamp == 0xFFFFFF) {
                chunk->extended_timestamp = 1;
                header_size += 4;
            }
            break;
            
        case 1: // Type 1 - 7 bytes
            if (len < 7 || !prev_chunk) return -1;
            
            chunk->timestamp_delta = read_uint24(data);
            chunk->length = read_uint24(data + 3);
            chunk->type = data[6];
            chunk->stream_id = prev_chunk->stream_id;
            
            header_size = 7;
            
            if (chunk->timestamp_delta == 0xFFFFFF) {
                chunk->extended_timestamp = 1;
                header_size += 4;
            }
            
            chunk->timestamp = prev_chunk->timestamp + chunk->timestamp_delta;
            break;
            
        case 2: // Type 2 - 3 bytes
            if (len < 3 || !prev_chunk) return -1;
            
            chunk->timestamp_delta = read_uint24(data);
            chunk->length = prev_chunk->length;
            chunk->type = prev_chunk->type;
            chunk->stream_id = prev_chunk->stream_id;
            
            header_size = 3;
            
            if (chunk->timestamp_delta == 0xFFFFFF) {
                chunk->extended_timestamp = 1;
                header_size += 4;
            }
            
            chunk->timestamp = prev_chunk->timestamp + chunk->timestamp_delta;
            break;
            
        case 3: // Type 3 - 0 bytes
            if (!prev_chunk) return -1;
            
            chunk->timestamp = prev_chunk->timestamp + prev_chunk->timestamp_delta;
            chunk->timestamp_delta = prev_chunk->timestamp_delta;
            chunk->length = prev_chunk->length;
            chunk->type = prev_chunk->type;
            chunk->stream_id = prev_chunk->stream_id;
            
            if (prev_chunk->extended_timestamp) {
                chunk->extended_timestamp = 1;
                header_size = 4;
            }
            break;
            
        default:
            LOG_ERROR("Invalid chunk type: %d", chunk->fmt);
            return -1;
    }
    
    // Read extended timestamp if present
    if (chunk->extended_timestamp && len >= header_size) {
        uint32_t ext_timestamp = read_uint32(data + header_size - 4);
        
        if (chunk->fmt == 0) {
            chunk->timestamp = ext_timestamp;
        } else {
            chunk->timestamp_delta = ext_timestamp;
            chunk->timestamp = (prev_chunk ? prev_chunk->timestamp : 0) + ext_timestamp;
        }
    }
    
    *bytes_read = header_size;
    LOG_DEBUG("Read message header: type=%d, len=%u, timestamp=%u", 
             chunk->type, chunk->length, chunk->timestamp);
    return 0;
}

int rtmp_chunk_read(RTMPChunkStream* cs, const uint8_t* data, size_t len, RTMPChunk* chunk, size_t* bytes_read) {
    if (!cs || !data || !len || !chunk || !bytes_read) return -1;
    
    *bytes_read = 0;
    size_t offset = 0;
    size_t header_bytes = 0;
    
    // Read basic header
    if (read_basic_header(data, len, chunk, &header_bytes) < 0) {
        LOG_ERROR("Failed to read basic header");
        return -1;
    }
    offset += header_bytes;
    
    // Read message header
    if (read_message_header(cs, data + offset, len - offset, chunk, &header_bytes) < 0) {
        LOG_ERROR("Failed to read message header");
        return -1;
    }
    offset += header_bytes;
    
    // Allocate or reallocate data buffer if needed
    if (!chunk->data || chunk->data_size < chunk->length) {
        uint8_t* new_data = buffer_realloc(chunk->data, chunk->length);
        if (!new_data) {
            LOG_ERROR("Failed to allocate chunk data buffer");
            return -1;
        }
        chunk->data = new_data;
        chunk->data_size = chunk->length;
    }
    
    // Calculate how many bytes we can read in this chunk
    size_t bytes_remaining = chunk->length - chunk->bytes_read;
    size_t can_read = bytes_remaining;
    if (can_read > cs->in_chunk_size) {
        can_read = cs->in_chunk_size;
    }
    if (can_read > len - offset) {
        can_read = len - offset;
    }
    
    // Copy data
    memcpy(chunk->data + chunk->bytes_read, data + offset, can_read);
    chunk->bytes_read += can_read;
    offset += can_read;
    
    // Update bytes received and check if we need to send acknowledgement
    cs->bytes_in += offset;
    if (cs->bytes_in - cs->last_ack >= cs->ack_window) {
        cs->last_ack = cs->bytes_in;
        // TODO: Send acknowledgement
    }
    
    // If chunk is complete, replace old chunk in array
    if (chunk->bytes_read == chunk->length) {
        RTMPChunk* prev_chunk = cs->chunks[chunk->csid];
        if (prev_chunk) {
            rtmp_chunk_destroy(prev_chunk);
        }
        cs->chunks[chunk->csid] = chunk;
        LOG_DEBUG("Completed chunk: csid=%u, type=%u, len=%u", 
                 chunk->csid, chunk->type, chunk->length);
    }
    
    *bytes_read = offset;
    return 0;
}

static int write_chunk_header(RTMPChunkStream* cs, RTMPChunk* chunk, uint8_t* buffer, size_t len, size_t* bytes_written) {
    size_t offset = 0;
    
    // Write basic header
    if (chunk->csid < 64) {
        if (len < 1) return -1;
        buffer[offset++] = (chunk->fmt << 6) | chunk->csid;
    } else if (chunk->csid < 320) {
        if (len < 2) return -1;
        buffer[offset++] = (chunk->fmt << 6);
        buffer[offset++] = chunk->csid - 64;
    } else {
        if (len < 3) return -1;
        buffer[offset++] = (chunk->fmt << 6) | 1;
        buffer[offset++] = (chunk->csid - 64) & 0xFF;
        buffer[offset++] = (chunk->csid - 64) >> 8;
    }
    
    // Write message header
    switch (chunk->fmt) {
        case 0:
            if (len - offset < 11) return -1;
            
            write_uint24(buffer + offset, chunk->timestamp >= 0xFFFFFF ? 0xFFFFFF : chunk->timestamp);
            write_uint24(buffer + offset + 3, chunk->length);
            buffer[offset + 6] = chunk->type;
            write_uint32(buffer + offset + 7, chunk->stream_id);
            offset += 11;
            
            if (chunk->timestamp >= 0xFFFFFF) {
                if (len - offset < 4) return -1;
                write_uint32(buffer + offset, chunk->timestamp);
                offset += 4;
            }
            break;
            
        case 1:
            if (len - offset < 7) return -1;
            
            write_uint24(buffer + offset, chunk->timestamp_delta >= 0xFFFFFF ? 0xFFFFFF : chunk->timestamp_delta);
            write_uint24(buffer + offset + 3, chunk->length);
            buffer[offset + 6] = chunk->type;
            offset += 7;
            
            if (chunk->timestamp_delta >= 0xFFFFFF) {
                if (len - offset < 4) return -1;
                write_uint32(buffer + offset, chunk->timestamp_delta);
                offset += 4;
            }
            break;
            
        case 2:
            if (len - offset < 3) return -1;
            
            write_uint24(buffer + offset, chunk->timestamp_delta >= 0xFFFFFF ? 0xFFFFFF : chunk->timestamp_delta);
            offset += 3;
            
            if (chunk->timestamp_delta >= 0xFFFFFF) {
                if (len - offset < 4) return -1;
                write_uint32(buffer + offset, chunk->timestamp_delta);
                offset += 4;
            }
            break;
            
        case 3:
            if (chunk->extended_timestamp) {
                if (len - offset < 4) return -1;
                write_uint32(buffer + offset, chunk->timestamp_delta);
                offset += 4;
            }
            break;
    }
    
    *bytes_written = offset;
    return 0;
}

int rtmp_chunk_write(RTMPChunkStream* cs, RTMPChunk* chunk, uint8_t* buffer, size_t len, size_t* bytes_written) {
    if (!cs || !chunk || !buffer || !len || !bytes_written) return -1;
    
    *bytes_written = 0;
    size_t offset = 0;
    size_t header_bytes = 0;
    
    // Write chunk header
    if (write_chunk_header(cs, chunk, buffer, len, &header_bytes) < 0) {
        LOG_ERROR("Failed to write chunk header");
        return -1;
    }
    offset += header_bytes;
    
    // Write chunk data
    size_t data_size = chunk->length - chunk->bytes_read;
    if (data_size > cs->out_chunk_size) {
        data_size = cs->out_chunk_size;
    }
    if (data_size > len - offset) {
        data_size = len - offset;
    }
    
    memcpy(buffer + offset, chunk->data + chunk->bytes_read, data_size);
    chunk->bytes_read += data_size;
    offset += data_size;
    
    cs->bytes_out += offset;
    *bytes_written = offset;
    
    LOG_DEBUG("Wrote chunk: csid=%u, type=%u, len=%u, bytes=%zu", 
             chunk->csid, chunk->type, chunk->length, offset);
             
    return 0;
}

int rtmp_chunk_update_size(RTMPChunkStream* cs, uint32_t size) {
    if (!cs) return -1;
    
    cs->chunk_size = size;
    cs->in_chunk_size = size;
    cs->out_chunk_size = size;
    
    LOG_INFO("Chunk size updated to %u", size);
    return 0;
}

int rtmp_chunk_acknowledge(RTMPChunkStream* cs, uint32_t size) {
    if (!cs) return -1;
    
    cs->ack_window = size;
    LOG_DEBUG("Acknowledgement window updated to %u", size);
    return 0;
}

void rtmp_chunk_reset_stream(RTMPChunkStream* cs, uint32_t csid) {
    if (!cs || csid >= RTMP_MAX_CHUNK_STREAMS) return;
    
    if (cs->chunks[csid]) {
        rtmp_chunk_destroy(cs->chunks[csid]);
        cs->chunks[csid] = NULL;
        LOG_DEBUG("Reset chunk stream: csid=%u", csid);
    }
}

int rtmp_chunk_process(RTMPChunkStream* cs, RTMPChunk* chunk) {
    if (!cs || !chunk) return -1;

    if (chunk->type == RTMP_MSG_CHUNK_SIZE) {
        if (chunk->length >= 4) {
            uint32_t new_size = read_uint32(chunk->data);
            rtmp_chunk_update_size(cs, new_size);
        }
    }
    else if (chunk->type == RTMP_MSG_WINDOW_ACK) {
        if (chunk->length >= 4) {
            uint32_t ack_size = read_uint32(chunk->data);
            rtmp_chunk_acknowledge(cs, ack_size);
        }
    }

    return 0;
}