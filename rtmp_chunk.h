#ifndef RTMP_CHUNK_H
#define RTMP_CHUNK_H

#include <stdint.h>
#include <stddef.h>
#include "rtmp_core.h"

// Chunk types
#define RTMP_CHUNK_TYPE_0 0  // Full message header
#define RTMP_CHUNK_TYPE_1 1  // Message header with no message ID
#define RTMP_CHUNK_TYPE_2 2  // Message header with timestamp delta
#define RTMP_CHUNK_TYPE_3 3  // No message header

// Basic chunk header lengths
#define RTMP_CHUNK_BASIC_HEADER_SIZE_1 1
#define RTMP_CHUNK_BASIC_HEADER_SIZE_2 2
#define RTMP_CHUNK_BASIC_HEADER_SIZE_3 3

// Maximum values
#define RTMP_CHUNK_MAX_HEADER_SIZE 18
#define RTMP_CHUNK_MAX_SIZE 65536

// Chunk structure
struct rtmp_chunk_s {
    uint8_t chunk_type;
    uint32_t chunk_stream_id;
    uint32_t timestamp;
    uint32_t msg_length;
    uint8_t msg_type_id;
    uint32_t msg_stream_id;
    uint8_t *msg_data;
};

// Chunk functions
size_t rtmp_chunk_read(rtmp_session_t *session, const uint8_t *data, size_t size, rtmp_chunk_t *chunk);
int rtmp_chunk_write(rtmp_session_t *session, const rtmp_chunk_t *chunk);

// Helper functions
size_t rtmp_chunk_get_header_size(uint8_t chunk_type);
size_t rtmp_chunk_parse_basic_header(const uint8_t *data, size_t size, uint8_t *chunk_type, uint32_t *chunk_stream_id);
int rtmp_chunk_create_basic_header(uint8_t chunk_type, uint32_t chunk_stream_id, uint8_t *out);

// Extended timestamp handling
int rtmp_chunk_read_extended_timestamp(const uint8_t *data, size_t size, uint32_t *timestamp);
int rtmp_chunk_write_extended_timestamp(uint8_t *data, uint32_t timestamp);

#endif // RTMP_CHUNK_H