#include "rtmp_amf.h"
#include "rtmp_utils.h"
#include <string.h>
#include <stdlib.h>

// AMF0 type markers
#define AMF0_NUMBER      0x00
#define AMF0_BOOLEAN     0x01
#define AMF0_STRING      0x02
#define AMF0_OBJECT      0x03
#define AMF0_NULL        0x05
#define AMF0_UNDEFINED   0x06
#define AMF0_REFERENCE   0x07
#define AMF0_ECMA_ARRAY  0x08
#define AMF0_OBJECT_END  0x09
#define AMF0_STRICT_ARRAY 0x0A
#define AMF0_DATE        0x0B
#define AMF0_LONG_STRING 0x0C

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
    size_t position;
} amf_buffer_t;

static amf_buffer_t* amf_buffer_create(size_t initial_capacity) {
    amf_buffer_t *buffer = (amf_buffer_t*)malloc(sizeof(amf_buffer_t));
    if (!buffer) return NULL;
    
    buffer->data = (uint8_t*)malloc(initial_capacity);
    if (!buffer->data) {
        free(buffer);
        return NULL;
    }
    
    buffer->size = 0;
    buffer->capacity = initial_capacity;
    buffer->position = 0;
    return buffer;
}

static void amf_buffer_destroy(amf_buffer_t *buffer) {
    if (buffer) {
        free(buffer->data);
        free(buffer);
    }
}

static int amf_buffer_ensure_capacity(amf_buffer_t *buffer, size_t additional) {
    if (buffer->size + additional > buffer->capacity) {
        size_t new_capacity = buffer->capacity * 2;
        if (new_capacity < buffer->size + additional) {
            new_capacity = buffer->size + additional;
        }
        
        uint8_t *new_data = (uint8_t*)realloc(buffer->data, new_capacity);
        if (!new_data) return -1;
        
        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }
    return 0;
}

// AMF0 Encoding Functions
static int amf0_encode_number(amf_buffer_t *buffer, double value) {
    if (amf_buffer_ensure_capacity(buffer, 9) < 0) return -1;
    
    buffer->data[buffer->size++] = AMF0_NUMBER;
    uint64_t be_value;
    memcpy(&be_value, &value, 8);
    be_value = RTMP_HTONLL(be_value);
    memcpy(buffer->data + buffer->size, &be_value, 8);
    buffer->size += 8;
    return 0;
}

static int amf0_encode_boolean(amf_buffer_t *buffer, uint8_t value) {
    if (amf_buffer_ensure_capacity(buffer, 2) < 0) return -1;
    
    buffer->data[buffer->size++] = AMF0_BOOLEAN;
    buffer->data[buffer->size++] = value ? 1 : 0;
    return 0;
}

static int amf0_encode_string(amf_buffer_t *buffer, const char *str, uint16_t len) {
    if (len > 0xFFFF) return -1;
    if (amf_buffer_ensure_capacity(buffer, len + 3) < 0) return -1;
    
    buffer->data[buffer->size++] = AMF0_STRING;
    buffer->data[buffer->size++] = (len >> 8) & 0xFF;
    buffer->data[buffer->size++] = len & 0xFF;
    memcpy(buffer->data + buffer->size, str, len);
    buffer->size += len;
    return 0;
}

static int amf0_encode_null(amf_buffer_t *buffer) {
    if (amf_buffer_ensure_capacity(buffer, 1) < 0) return -1;
    buffer->data[buffer->size++] = AMF0_NULL;
    return 0;
}

// AMF0 Decoding Functions
static int amf0_decode_number(const uint8_t *data, size_t size, size_t *offset, double *value) {
    if (size - *offset < 9) return -1;
    if (data[*offset] != AMF0_NUMBER) return -1;
    
    (*offset)++;
    uint64_t be_value;
    memcpy(&be_value, data + *offset, 8);
    be_value = RTMP_NTOHLL(be_value);
    memcpy(value, &be_value, 8);
    *offset += 8;
    return 0;
}

static int amf0_decode_boolean(const uint8_t *data, size_t size, size_t *offset, uint8_t *value) {
    if (size - *offset < 2) return -1;
    if (data[*offset] != AMF0_BOOLEAN) return -1;
    
    (*offset)++;
    *value = data[*offset] != 0;
    (*offset)++;
    return 0;
}

static int amf0_decode_string(const uint8_t *data, size_t size, size_t *offset, char **str, uint16_t *len) {
    if (size - *offset < 3) return -1;
    if (data[*offset] != AMF0_STRING) return -1;
    
    (*offset)++;
    *len = (data[*offset] << 8) | data[*offset + 1];
    *offset += 2;
    
    if (size - *offset < *len) return -1;
    
    *str = (char*)malloc(*len + 1);
    if (!*str) return -1;
    
    memcpy(*str, data + *offset, *len);
    (*str)[*len] = '\0';
    *offset += *len;
    return 0;
}

// Public API Implementation
rtmp_amf_t* rtmp_amf_create(void) {
    amf_buffer_t *buffer = amf_buffer_create(1024);
    return (rtmp_amf_t*)buffer;
}

void rtmp_amf_destroy(rtmp_amf_t *amf) {
    amf_buffer_destroy((amf_buffer_t*)amf);
}

int rtmp_amf_encode_number(rtmp_amf_t *amf, double value) {
    return amf0_encode_number((amf_buffer_t*)amf, value);
}

int rtmp_amf_encode_boolean(rtmp_amf_t *amf, int value) {
    return amf0_encode_boolean((amf_buffer_t*)amf, value ? 1 : 0);
}

int rtmp_amf_encode_string(rtmp_amf_t *amf, const char *str) {
    uint16_t len = (uint16_t)strlen(str);
    return amf0_encode_string((amf_buffer_t*)amf, str, len);
}

int rtmp_amf_encode_null(rtmp_amf_t *amf) {
    return amf0_encode_null((amf_buffer_t*)amf);
}

const uint8_t* rtmp_amf_get_data(rtmp_amf_t *amf, size_t *size) {
    amf_buffer_t *buffer = (amf_buffer_t*)amf;
    if (size) *size = buffer->size;
    return buffer->data;
}

int rtmp_amf_decode_number(const uint8_t *data, size_t size, size_t *offset, double *value) {
    return amf0_decode_number(data, size, offset, value);
}

int rtmp_amf_decode_boolean(const uint8_t *data, size_t size, size_t *offset, int *value) {
    uint8_t bool_value;
    int result = amf0_decode_boolean(data, size, offset, &bool_value);
    if (result == 0) *value = bool_value != 0;
    return result;
}

int rtmp_amf_decode_string(const uint8_t *data, size_t size, size_t *offset, char **str) {
    uint16_t len;
    return amf0_decode_string(data, size, offset, str, &len);
}

// Object handling
int rtmp_amf_begin_object(rtmp_amf_t *amf) {
    amf_buffer_t *buffer = (amf_buffer_t*)amf;
    if (amf_buffer_ensure_capacity(buffer, 1) < 0) return -1;
    buffer->data[buffer->size++] = AMF0_OBJECT;
    return 0;
}

int rtmp_amf_end_object(rtmp_amf_t *amf) {
    amf_buffer_t *buffer = (amf_buffer_t*)amf;
    if (amf_buffer_ensure_capacity(buffer, 3) < 0) return -1;
    buffer->data[buffer->size++] = 0x00;
    buffer->data[buffer->size++] = 0x00;
    buffer->data[buffer->size++] = AMF0_OBJECT_END;
    return 0;
}