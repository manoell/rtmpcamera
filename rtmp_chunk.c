#include "rtmp_chunk.h"
#include "rtmp_utils.h"
#include <string.h>
#include <stdlib.h>

size_t rtmp_chunk_read(rtmp_session_t *session, const uint8_t *data, size_t size, rtmp_chunk_t *chunk) {
    if (!session || !data || !size || !chunk) return 0;
    
    size_t offset = 0;
    
    // Parse basic header
    uint8_t chunk_type;
    uint32_t chunk_stream_id;
    size_t basic_header_size = rtmp_chunk_parse_basic_header(data, size, &chunk_type, &chunk_stream_id);
    if (basic_header_size == 0) return 0;
    
    offset += basic_header_size;
    if (offset >= size) return 0;
    
    // Get chunk stream
    rtmp_chunk_stream_t *chunk_stream = rtmp_get_chunk_stream(session, chunk_stream_id);
    if (!chunk_stream) return (size_t)-1;
    
    // Parse message header
    size_t header_size = rtmp_chunk_get_header_size(chunk_type);
    if (offset + header_size > size) return 0;
    
    const uint8_t *header = data + offset;
    offset += header_size;
    
    uint32_t timestamp = 0;
    switch (chunk_type) {
        case RTMP_CHUNK_TYPE_0: {
            timestamp = RTMP_NTOH24(header);
            chunk_stream->msg_length = RTMP_NTOH24(header + 3);
            chunk_stream->msg_type_id = header[6];
            chunk_stream->msg_stream_id = RTMP_NTOHL(*(uint32_t*)(header + 7));
            break;
        }
        
        case RTMP_CHUNK_TYPE_1: {
            timestamp = RTMP_NTOH24(header);
            chunk_stream->msg_length = RTMP_NTOH24(header + 3);
            chunk_stream->msg_type_id = header[6];
            break;
        }
        
        case RTMP_CHUNK_TYPE_2: {
            timestamp = RTMP_NTOH24(header);
            break;
        }
        
        case RTMP_CHUNK_TYPE_3:
            break;
    }
    
    // Handle extended timestamp
    if (timestamp >= 0xFFFFFF) {
        if (offset + 4 > size) return 0;
        
        uint32_t extended_timestamp;
        if (rtmp_chunk_read_extended_timestamp(data + offset, size - offset, &extended_timestamp) < 0) {
            return (size_t)-1;
        }
        
        timestamp = extended_timestamp;
        offset += 4;
    }
    
    // Update timestamp
    if (chunk_type != RTMP_CHUNK_TYPE_0) {
        timestamp += chunk_stream->timestamp;
    }
    chunk_stream->timestamp = timestamp;
    
    // Calculate chunk data size
    size_t chunk_data_size = chunk_stream->msg_length - chunk_stream->msg_data_pos;
    if (chunk_data_size > session->in_chunk_size) {
        chunk_data_size = session->in_chunk_size;
    }
    
    if (offset + chunk_data_size > size) return 0;
    
    // Allocate message data if needed
    if (!chunk_stream->msg_data) {
        chunk_stream->msg_data = (uint8_t*)malloc(chunk_stream->msg_length);
        if (!chunk_stream->msg_data) return (size_t)-1;
        chunk_stream->msg_data_pos = 0;
    }
    
    // Copy chunk data
    memcpy(chunk_stream->msg_data + chunk_stream->msg_data_pos,
           data + offset,
           chunk_data_size);
    
    chunk_stream->msg_data_pos += chunk_data_size;
    offset += chunk_data_size;
    
    // Check if message is complete
    if (chunk_stream->msg_data_pos >= chunk_stream->msg_length) {
        // Fill chunk structure
        chunk->chunk_type = chunk_type;
        chunk->chunk_stream_id = chunk_stream_id;
        chunk->timestamp = chunk_stream->timestamp;
        chunk->msg_length = chunk_stream->msg_length;
        chunk->msg_type_id = chunk_stream->msg_type_id;
        chunk->msg_stream_id = chunk_stream->msg_stream_id;
        chunk->msg_data = chunk_stream->msg_data;
        
        // Reset chunk stream
        chunk_stream->msg_data = NULL;
        chunk_stream->msg_data_pos = 0;
    } else {
        // Message not complete yet
        chunk->msg_data = NULL;
    }
    
    return offset;
}

int rtmp_chunk_write(rtmp_session_t *session, const rtmp_chunk_t *chunk) {
    if (!session || !chunk) return -1;
    
    uint8_t header[RTMP_CHUNK_MAX_HEADER_SIZE];
    size_t header_size = 0;
    
    // Write basic header
    size_t basic_header_size = rtmp_chunk_create_basic_header(chunk->chunk_type,
                                                            chunk->chunk_stream_id,
                                                            header);
    header_size += basic_header_size;
    
    // Write message header
    switch (chunk->chunk_type) {
        case RTMP_CHUNK_TYPE_0: {
            uint32_t timestamp = chunk->timestamp >= 0xFFFFFF ? 0xFFFFFF : chunk->timestamp;
            uint32_t be_timestamp = RTMP_HTON24(timestamp);
            uint32_t be_length = RTMP_HTON24(chunk->msg_length);
            uint32_t be_stream_id = RTMP_HTONL(chunk->msg_stream_id);
            
            memcpy(header + header_size, &be_timestamp, 3);
            memcpy(header + header_size + 3, &be_length, 3);
            header[header_size + 6] = chunk->msg_type_id;
            memcpy(header + header_size + 7, &be_stream_id, 4);
            header_size += 11;
            break;
        }
        
        case RTMP_CHUNK_TYPE_1: {
            uint32_t timestamp = chunk->timestamp >= 0xFFFFFF ? 0xFFFFFF : chunk->timestamp;
            uint32_t be_timestamp = RTMP_HTON24(timestamp);
            uint32_t be_length = RTMP_HTON24(chunk->msg_length);
            
            memcpy(header + header_size, &be_timestamp, 3);
            memcpy(header + header_size + 3, &be_length, 3);
            header[header_size + 6] = chunk->msg_type_id;
            header_size += 7;
            break;
        }
        
        case RTMP_CHUNK_TYPE_2: {
            uint32_t timestamp = chunk->timestamp >= 0xFFFFFF ? 0xFFFFFF : chunk->timestamp;
            uint32_t be_timestamp = RTMP_HTON24(timestamp);
            memcpy(header + header_size, &be_timestamp, 3);
            header_size += 3;
            break;
        }
    }
    
    // Write extended timestamp if needed
    if (chunk->timestamp >= 0xFFFFFF) {
        uint32_t be_timestamp = RTMP_HTONL(chunk->timestamp);
        memcpy(header + header_size, &be_timestamp, 4);
        header_size += 4;
    }
    
    // Send header
    if (rtmp_session_send_data(session, header, header_size) < 0) {
        return -1;
    }
    
    // Send chunk data
    if (chunk->msg_data && chunk->msg_length > 0) {
        size_t offset = 0;
        while (offset < chunk->msg_length) {
            size_t remaining = chunk->msg_length - offset;
            size_t chunk_size = remaining > session->out_chunk_size ? 
                              session->out_chunk_size : remaining;
            
            if (rtmp_session_send_data(session, chunk->msg_data + offset, chunk_size) < 0) {
                return -1;
            }
            
            offset += chunk_size;
            
            // If more data remains, send continuation chunks
            if (offset < chunk->msg_length) {
                uint8_t continuation_header;
                rtmp_chunk_create_basic_header(RTMP_CHUNK_TYPE_3,
                                           chunk->chunk_stream_id,
                                           &continuation_header);
                
                if (rtmp_session_send_data(session, &continuation_header, 1) < 0) {
                    return -1;
                }
            }
        }
    }
    
    return 0;
}

size_t rtmp_chunk_get_header_size(uint8_t chunk_type) {
    switch (chunk_type) {
        case RTMP_CHUNK_TYPE_0:
            return 11;
        case RTMP_CHUNK_TYPE_1:
            return 7;
        case RTMP_CHUNK_TYPE_2:
            return 3;
        case RTMP_CHUNK_TYPE_3:
            return 0;
        default:
            return 0;
    }
}

size_t rtmp_chunk_parse_basic_header(const uint8_t *data, size_t size,
                                   uint8_t *chunk_type, uint32_t *chunk_stream_id) {
    if (!data || !size || !chunk_type || !chunk_stream_id) return 0;
    
    uint8_t fmt = (data[0] >> 6) & 0x03;
    uint32_t csid = data[0] & 0x3F;
    
    size_t header_size = 1;
    
    if (csid == 0) {
        if (size < 2) return 0;
        csid = data[1] + 64;
        header_size = 2;
    } else if (csid == 1) {
        if (size < 3) return 0;
        csid = (data[2] << 8) + data[1] + 64;
        header_size = 3;
    }
    
    *chunk_type = fmt;
    *chunk_stream_id = csid;
    
    return header_size;
}

int rtmp_chunk_create_basic_header(uint8_t chunk_type, uint32_t chunk_stream_id, uint8_t *out) {
    if (!out) return -1;
    
    if (chunk_stream_id >= 64 + 255) {
        out[0] = (chunk_type << 6) | 1;
        chunk_stream_id -= 64;
        out[1] = chunk_stream_id & 0xFF;
        out[2] = (chunk_stream_id >> 8) & 0xFF;
        return 3;
    } else if (chunk_stream_id >= 64) {
        out[0] = (chunk_type << 6) | 0;
        out[1] = chunk_stream_id - 64;
        return 2;
    } else {
        out[0] = (chunk_type << 6) | chunk_stream_id;
        return 1;
    }
}

int rtmp_chunk_read_extended_timestamp(const uint8_t *data, size_t size, uint32_t *timestamp) {
    if (!data || size < 4 || !timestamp) return -1;
    
    memcpy(timestamp, data, 4);
    *timestamp = RTMP_NTOHL(*timestamp);
    
    return 0;
}

int rtmp_chunk_write_extended_timestamp(uint8_t *data, uint32_t timestamp) {
    if (!data) return -1;
    
    uint32_t be_timestamp = RTMP_HTONL(timestamp);
    memcpy(data, &be_timestamp, 4);
    
    return 0;
}