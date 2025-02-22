#ifndef _RTMP_CHUNK_H
#define _RTMP_CHUNK_H

#include <stdint.h>
#include <stdbool.h>

// Chunk types
#define RTMP_CHUNK_TYPE_0 0  // Full chunk
#define RTMP_CHUNK_TYPE_1 1  // No message ID
#define RTMP_CHUNK_TYPE_2 2  // Timestamp only
#define RTMP_CHUNK_TYPE_3 3  // No header

// Default chunk size
#define RTMP_DEFAULT_CHUNK_SIZE 128

// Maximum chunk size
#define RTMP_MAX_CHUNK_SIZE 65536

// Error codes
typedef enum {
    RTMP_CHUNK_OK = 0,
    RTMP_CHUNK_ERROR_INVALID_TYPE = -1,
    RTMP_CHUNK_ERROR_SIZE_EXCEEDED = -2,
    RTMP_CHUNK_ERROR_MEMORY = -3,
    RTMP_CHUNK_ERROR_INCOMPLETE = -4
} rtmp_chunk_error_t;

// Chunk header structure
typedef struct {
    uint8_t type;              // Chunk type (0-3)
    uint32_t timestamp;        // Timestamp 
    uint32_t message_length;   // Length of message
    uint8_t message_type;      // Type of message
    uint32_t stream_id;        // Stream ID
} rtmp_chunk_header_t;

// Chunk structure
typedef struct {
    rtmp_chunk_header_t header;
    uint8_t *data;            // Chunk data
    size_t data_size;         // Current size of data
    size_t capacity;          // Allocated capacity
} rtmp_chunk_t;

// Chunk functions
rtmp_chunk_t* rtmp_chunk_create(void);
void rtmp_chunk_destroy(rtmp_chunk_t *chunk);

// Set chunk properties
rtmp_chunk_error_t rtmp_chunk_set_type(rtmp_chunk_t *chunk, uint8_t type);
rtmp_chunk_error_t rtmp_chunk_set_timestamp(rtmp_chunk_t *chunk, uint32_t timestamp);
rtmp_chunk_error_t rtmp_chunk_set_message_length(rtmp_chunk_t *chunk, uint32_t length);
rtmp_chunk_error_t rtmp_chunk_set_message_type(rtmp_chunk_t *chunk, uint8_t type);
rtmp_chunk_error_t rtmp_chunk_set_stream_id(rtmp_chunk_t *chunk, uint32_t stream_id);

// Data handling
rtmp_chunk_error_t rtmp_chunk_append_data(rtmp_chunk_t *chunk, const uint8_t *data, size_t size);
rtmp_chunk_error_t rtmp_chunk_clear_data(rtmp_chunk_t *chunk);
rtmp_chunk_error_t rtmp_chunk_resize(rtmp_chunk_t *chunk, size_t new_size);

// Chunk operations
rtmp_chunk_error_t rtmp_chunk_split(rtmp_chunk_t *input, rtmp_chunk_t **chunks, size_t chunk_size, size_t *num_chunks);
rtmp_chunk_error_t rtmp_chunk_merge(rtmp_chunk_t **chunks, size_t num_chunks, rtmp_chunk_t *output);

// Validation
bool rtmp_chunk_is_complete(const rtmp_chunk_t *chunk);
bool rtmp_chunk_is_valid(const rtmp_chunk_t *chunk);

// Utility functions
size_t rtmp_chunk_get_header_size(uint8_t chunk_type);
const char* rtmp_chunk_get_error_string(rtmp_chunk_error_t error);

#endif /* _RTMP_CHUNK_H */