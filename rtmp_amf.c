// rtmp_amf.c
#include "rtmp_amf.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

RTMPBuffer* rtmp_buffer_create(size_t initial_size) {
    RTMPBuffer* buffer = calloc(1, sizeof(RTMPBuffer));
    if (!buffer) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to allocate AMF buffer");
        return NULL;
    }

    buffer->data = malloc(initial_size);
    if (!buffer->data) {
        free(buffer);
        rtmp_log(RTMP_LOG_ERROR, "Failed to allocate AMF buffer data");
        return NULL;
    }

    buffer->capacity = initial_size;
    buffer->size = 0;
    buffer->position = 0;

    return buffer;
}

void rtmp_buffer_destroy(RTMPBuffer* buffer) {
    if (buffer) {
        free(buffer->data);
        free(buffer);
    }
}

int rtmp_buffer_ensure_space(RTMPBuffer* buffer, size_t additional) {
    if (!buffer) return RTMP_ERROR_MEMORY;

    if (buffer->size + additional > buffer->capacity) {
        size_t new_capacity = buffer->capacity * 2;
        while (new_capacity < buffer->size + additional) {
            new_capacity *= 2;
        }

        uint8_t* new_data = realloc(buffer->data, new_capacity);
        if (!new_data) {
            rtmp_log(RTMP_LOG_ERROR, "Failed to reallocate AMF buffer");
            return RTMP_ERROR_MEMORY;
        }

        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }

    return RTMP_OK;
}

// Funções de encoding
int rtmp_amf0_write_string(RTMPBuffer* buffer, const char* str) {
    if (!buffer || !str) return RTMP_ERROR_MEMORY;

    size_t str_len = strlen(str);
    size_t needed = 1 + 2 + str_len; // type + length + string
    
    int ret = rtmp_buffer_ensure_space(buffer, needed);
    if (ret != RTMP_OK) return ret;

    buffer->data[buffer->size++] = AMF0_STRING;
    
    uint16_t len_be = htons((uint16_t)str_len);
    memcpy(buffer->data + buffer->size, &len_be, 2);
    buffer->size += 2;
    
    memcpy(buffer->data + buffer->size, str, str_len);
    buffer->size += str_len;

    rtmp_log(RTMP_LOG_DEBUG, "AMF wrote string: %s (len: %zu)", str, str_len);
    return RTMP_OK;
}

int rtmp_amf0_write_number(RTMPBuffer* buffer, double number) {
    if (!buffer) return RTMP_ERROR_MEMORY;

    int ret = rtmp_buffer_ensure_space(buffer, 1 + 8); // type + number
    if (ret != RTMP_OK) return ret;

    buffer->data[buffer->size++] = AMF0_NUMBER;
    
    // Convert to network byte order (big endian)
    uint64_t be_number;
    memcpy(&be_number, &number, 8);
    be_number = __builtin_bswap64(be_number);
    memcpy(buffer->data + buffer->size, &be_number, 8);
    buffer->size += 8;

    rtmp_log(RTMP_LOG_DEBUG, "AMF wrote number: %f", number);
    return RTMP_OK;
}

int rtmp_amf0_write_boolean(RTMPBuffer* buffer, uint8_t boolean) {
    if (!buffer) return RTMP_ERROR_MEMORY;

    int ret = rtmp_buffer_ensure_space(buffer, 2); // type + boolean
    if (ret != RTMP_OK) return ret;

    buffer->data[buffer->size++] = AMF0_BOOLEAN;
    buffer->data[buffer->size++] = boolean ? 1 : 0;

    rtmp_log(RTMP_LOG_DEBUG, "AMF wrote boolean: %d", boolean);
    return RTMP_OK;
}

int rtmp_amf0_write_null(RTMPBuffer* buffer) {
    if (!buffer) return RTMP_ERROR_MEMORY;

    int ret = rtmp_buffer_ensure_space(buffer, 1); // type only
    if (ret != RTMP_OK) return ret;

    buffer->data[buffer->size++] = AMF0_NULL;
    
    rtmp_log(RTMP_LOG_DEBUG, "AMF wrote null");
    return RTMP_OK;
}

// Funções de decoding
int rtmp_amf0_read_string(RTMPBuffer* buffer, char** str, uint16_t* len) {
    if (!buffer || !str || !len) return RTMP_ERROR_MEMORY;
    if (buffer->position >= buffer->size) return RTMP_ERROR_PROTOCOL;

    uint8_t type = buffer->data[buffer->position++];
    if (type != AMF0_STRING) {
        rtmp_log(RTMP_LOG_ERROR, "Expected string type, got %02x", type);
        return RTMP_ERROR_PROTOCOL;
    }

    if (buffer->position + 2 > buffer->size) return RTMP_ERROR_PROTOCOL;
    
    memcpy(len, buffer->data + buffer->position, 2);
    *len = ntohs(*len);
    buffer->position += 2;

    if (buffer->position + *len > buffer->size) return RTMP_ERROR_PROTOCOL;

    *str = malloc(*len + 1);
    if (!*str) return RTMP_ERROR_MEMORY;

    memcpy(*str, buffer->data + buffer->position, *len);
    (*str)[*len] = '\0';
    buffer->position += *len;

    rtmp_log(RTMP_LOG_DEBUG, "AMF read string: %s (len: %u)", *str, *len);
    return RTMP_OK;
}

int rtmp_amf0_read_number(RTMPBuffer* buffer, double* number) {
    if (!buffer || !number) return RTMP_ERROR_MEMORY;
    if (buffer->position >= buffer->size) return RTMP_ERROR_PROTOCOL;

    uint8_t type = buffer->data[buffer->position++];
    if (type != AMF0_NUMBER) {
        rtmp_log(RTMP_LOG_ERROR, "Expected number type, got %02x", type);
        return RTMP_ERROR_PROTOCOL;
    }

    if (buffer->position + 8 > buffer->size) return RTMP_ERROR_PROTOCOL;

    uint64_t be_number;
    memcpy(&be_number, buffer->data + buffer->position, 8);
    be_number = __builtin_bswap64(be_number);
    memcpy(number, &be_number, 8);
    buffer->position += 8;

    rtmp_log(RTMP_LOG_DEBUG, "AMF read number: %f", *number);
    return RTMP_OK;
}

int rtmp_amf0_read_boolean(RTMPBuffer* buffer, uint8_t* boolean) {
    if (!buffer || !boolean) return RTMP_ERROR_MEMORY;
    if (buffer->position >= buffer->size) return RTMP_ERROR_PROTOCOL;

    uint8_t type = buffer->data[buffer->position++];
    if (type != AMF0_BOOLEAN) {
        rtmp_log(RTMP_LOG_ERROR, "Expected boolean type, got %02x", type);
        return RTMP_ERROR_PROTOCOL;
    }

    if (buffer->position >= buffer->size) return RTMP_ERROR_PROTOCOL;
    
    *boolean = buffer->data[buffer->position++];

    rtmp_log(RTMP_LOG_DEBUG, "AMF read boolean: %d", *boolean);
    return RTMP_OK;
}