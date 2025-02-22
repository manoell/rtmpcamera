#include "rtmp_chunk.h"
#include <stdlib.h>
#include <string.h>

// Helper function for memory allocation
static uint8_t* allocate_buffer(size_t size) {
    uint8_t *buffer = (uint8_t*)malloc(size);
    if (buffer) {
        memset(buffer, 0, size);
    }
    return buffer;
}

rtmp_chunk_t* rtmp_chunk_create(void) {
    rtmp_chunk_t *chunk = (rtmp_chunk_t*)malloc(sizeof(rtmp_chunk_t));
    if (!chunk) {
        return NULL;
    }

    memset(chunk, 0, sizeof(rtmp_chunk_t));
    chunk->capacity = RTMP_DEFAULT_CHUNK_SIZE;
    chunk->data = allocate_buffer(chunk->capacity);
    
    if (!chunk->data) {
        free(chunk);
        return NULL;
    }

    return chunk;
}

void rtmp_chunk_destroy(rtmp_chunk_t *chunk) {
    if (chunk) {
        if (chunk->data) {
            free(chunk->data);
        }
        free(chunk);
    }
}

rtmp_chunk_error_t rtmp_chunk_set_type(rtmp_chunk_t *chunk, uint8_t type) {
    if (!chunk) return RTMP_CHUNK_ERROR_INVALID_TYPE;
    if (type > RTMP_CHUNK_TYPE_3) return RTMP_CHUNK_ERROR_INVALID_TYPE;
    
    chunk->header.type = type;
    return RTMP_CHUNK_OK;
}

rtmp_chunk_error_t rtmp_chunk_set_timestamp(rtmp_chunk_t *chunk, uint32_t timestamp) {
    if (!chunk) return RTMP_CHUNK_ERROR_INVALID_TYPE;
    
    chunk->header.timestamp = timestamp;
    return RTMP_CHUNK_OK;
}

rtmp_chunk_error_t rtmp_chunk_set_message_length(rtmp_chunk_t *chunk, uint32_t length) {
    if (!chunk) return RTMP_CHUNK_ERROR_INVALID_TYPE;
    if (length > RTMP_MAX_CHUNK_SIZE) return RTMP_CHUNK_ERROR_SIZE_EXCEEDED;
    
    chunk->header.message_length = length;
    return RTMP_CHUNK_OK;
}

rtmp_chunk_error_t rtmp_chunk_set_message_type(rtmp_chunk_t *chunk, uint8_t type) {
    if (!chunk) return RTMP_CHUNK_ERROR_INVALID_TYPE;
    
    chunk->header.message_type = type;
    return RTMP_CHUNK_OK;
}

rtmp_chunk_error_t rtmp_chunk_set_stream_id(rtmp_chunk_t *chunk, uint32_t stream_id) {
    if (!chunk) return RTMP_CHUNK_ERROR_INVALID_TYPE;
    
    chunk->header.stream_id = stream_id;
    return RTMP_CHUNK_OK;
}

rtmp_chunk_error_t rtmp_chunk_append_data(rtmp_chunk_t *chunk, const uint8_t *data, size_t size) {
    if (!chunk || !data) return RTMP_CHUNK_ERROR_INVALID_TYPE;
    
    // Check if we need to resize
    if (chunk->data_size + size > chunk->capacity) {
        size_t new_capacity = chunk->capacity * 2;
        while (new_capacity < chunk->data_size + size) {
            new_capacity *= 2;
        }
        
        rtmp_chunk_error_t err = rtmp_chunk_resize(chunk, new_capacity);
        if (err != RTMP_CHUNK_OK) return err;
    }
    
    memcpy(chunk->data + chunk->data_size, data, size);
    chunk->data_size += size;
    return RTMP_CHUNK_OK;
}

rtmp_chunk_error_t rtmp_chunk_clear_data(rtmp_chunk_t *chunk) {
    if (!chunk) return RTMP_CHUNK_ERROR_INVALID_TYPE;
    
    if (chunk->data) {
        memset(chunk->data, 0, chunk->capacity);
    }
    chunk->data_size = 0;
    return RTMP_CHUNK_OK;
}

rtmp_chunk_error_t rtmp_chunk_resize(rtmp_chunk_t *chunk, size_t new_size) {
    if (!chunk) return RTMP_CHUNK_ERROR_INVALID_TYPE;
    if (new_size > RTMP_MAX_CHUNK_SIZE) return RTMP_CHUNK_ERROR_SIZE_EXCEEDED;
    
    uint8_t *new_data = allocate_buffer(new_size);
    if (!new_data) return RTMP_CHUNK_ERROR_MEMORY;
    
    if (chunk->data && chunk->data_size > 0) {
        memcpy(new_data, chunk->data, chunk->data_size);
        free(chunk->data);
    }
    
    chunk->data = new_data;
    chunk->capacity = new_size;
    return RTMP_CHUNK_OK;
}

rtmp_chunk_error_t rtmp_chunk_split(rtmp_chunk_t *input, rtmp_chunk_t **chunks, size_t chunk_size, size_t *num_chunks) {
    if (!input || !chunks || !num_chunks) return RTMP_CHUNK_ERROR_INVALID_TYPE;
    if (chunk_size == 0 || chunk_size > RTMP_MAX_CHUNK_SIZE) return RTMP_CHUNK_ERROR_SIZE_EXCEEDED;
    
    size_t total_chunks = (input->data_size + chunk_size - 1) / chunk_size;
    *num_chunks = total_chunks;
    
    for (size_t i = 0; i < total_chunks; i++) {
        chunks[i] = rtmp_chunk_create();
        if (!chunks[i]) {
            // Cleanup already allocated chunks
            for (size_t j = 0; j < i; j++) {
                rtmp_chunk_destroy(chunks[j]);
            }
            return RTMP_CHUNK_ERROR_MEMORY;
        }
        
        // Copy header
        chunks[i]->header = input->header;
        
        // Calculate chunk data size
        size_t offset = i * chunk_size;
        size_t size = (i == total_chunks - 1) ? 
                     (input->data_size - offset) : chunk_size;
        
        // Copy data
        rtmp_chunk_error_t err = rtmp_chunk_append_data(chunks[i], 
                                                       input->data + offset, 
                                                       size);
        if (err != RTMP_CHUNK_OK) {
            // Cleanup on error
            for (size_t j = 0; j <= i; j++) {
                rtmp_chunk_destroy(chunks[j]);
            }
            return err;
        }
    }
    
    return RTMP_CHUNK_OK;
}

rtmp_chunk_error_t rtmp_chunk_merge(rtmp_chunk_t **chunks, size_t num_chunks, rtmp_chunk_t *output) {
    if (!chunks || !output || num_chunks == 0) return RTMP_CHUNK_ERROR_INVALID_TYPE;
    
    // Clear output chunk
    rtmp_chunk_clear_data(output);
    
    // Copy header from first chunk
    output->header = chunks[0]->header;
    
    // Merge data from all chunks
    for (size_t i = 0; i < num_chunks; i++) {
        rtmp_chunk_error_t err = rtmp_chunk_append_data(output, 
                                                       chunks[i]->data, 
                                                       chunks[i]->data_size);
        if (err != RTMP_CHUNK_OK) return err;
    }
    
    return RTMP_CHUNK_OK;
}

bool rtmp_chunk_is_complete(const rtmp_chunk_t *chunk) {
    if (!chunk) return false;
    return chunk->data_size == chunk->header.message_length;
}

bool rtmp_chunk_is_valid(const rtmp_chunk_t *chunk) {
    if (!chunk) return false;
    if (chunk->header.type > RTMP_CHUNK_TYPE_3) return false;
    if (chunk->data_size > RTMP_MAX_CHUNK_SIZE) return false;
    if (!chunk->data && chunk->data_size > 0) return false;
    return true;
}

size_t rtmp_chunk_get_header_size(uint8_t chunk_type) {
    switch (chunk_type) {
        case RTMP_CHUNK_TYPE_0:
            return 11;  // Basic header + message header
        case RTMP_CHUNK_TYPE_1:
            return 7;   // Basic header + timestamp + length + type
        case RTMP_CHUNK_TYPE_2:
            return 3;   // Basic header + timestamp
        case RTMP_CHUNK_TYPE_3:
            return 0;   // No header
        default:
            return 0;
    }
}

const char* rtmp_chunk_get_error_string(rtmp_chunk_error_t error) {
    switch (error) {
        case RTMP_CHUNK_OK:
            return "Success";
        case RTMP_CHUNK_ERROR_INVALID_TYPE:
            return "Invalid chunk type";
        case RTMP_CHUNK_ERROR_SIZE_EXCEEDED:
            return "Maximum chunk size exceeded";
        case RTMP_CHUNK_ERROR_MEMORY:
            return "Memory allocation failed";
        case RTMP_CHUNK_ERROR_INCOMPLETE:
            return "Chunk is incomplete";
        default:
            return "Unknown error";
    }
}