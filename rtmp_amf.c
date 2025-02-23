#include <string.h>
#include <stdlib.h>
#include "rtmp_amf.h"
#include "rtmp_utils.h"

#define AMF_MAX_STRING_LEN 65535
#define AMF_NUMBER_SIZE 9
#define AMF_BOOLEAN_SIZE 2

static void write_byte(uint8_t **buffer, uint8_t value) {
    **buffer = value;
    (*buffer)++;
}

static void write_be16(uint8_t **buffer, uint16_t value) {
    (*buffer)[0] = (value >> 8) & 0xFF;
    (*buffer)[1] = value & 0xFF;
    *buffer += 2;
}

static void write_be32(uint8_t **buffer, uint32_t value) {
    (*buffer)[0] = (value >> 24) & 0xFF;
    (*buffer)[1] = (value >> 16) & 0xFF;
    (*buffer)[2] = (value >> 8) & 0xFF;
    (*buffer)[3] = value & 0xFF;
    *buffer += 4;
}

static void write_be64(uint8_t **buffer, uint64_t value) {
    (*buffer)[0] = (value >> 56) & 0xFF;
    (*buffer)[1] = (value >> 48) & 0xFF;
    (*buffer)[2] = (value >> 40) & 0xFF;
    (*buffer)[3] = (value >> 32) & 0xFF;
    (*buffer)[4] = (value >> 24) & 0xFF;
    (*buffer)[5] = (value >> 16) & 0xFF;
    (*buffer)[6] = (value >> 8) & 0xFF;
    (*buffer)[7] = value & 0xFF;
    *buffer += 8;
}

static uint8_t read_byte(const uint8_t **buffer) {
    uint8_t value = **buffer;
    (*buffer)++;
    return value;
}

static uint16_t read_be16(const uint8_t **buffer) {
    uint16_t value = ((*buffer)[0] << 8) | (*buffer)[1];
    *buffer += 2;
    return value;
}

static uint32_t read_be32(const uint8_t **buffer) {
    uint32_t value = ((*buffer)[0] << 24) | ((*buffer)[1] << 16) |
                     ((*buffer)[2] << 8) | (*buffer)[3];
    *buffer += 4;
    return value;
}

static uint64_t read_be64(const uint8_t **buffer) {
    uint64_t value = ((uint64_t)(*buffer)[0] << 56) |
                     ((uint64_t)(*buffer)[1] << 48) |
                     ((uint64_t)(*buffer)[2] << 40) |
                     ((uint64_t)(*buffer)[3] << 32) |
                     ((uint64_t)(*buffer)[4] << 24) |
                     ((uint64_t)(*buffer)[5] << 16) |
                     ((uint64_t)(*buffer)[6] << 8) |
                     (*buffer)[7];
    *buffer += 8;
    return value;
}

int rtmp_amf_encode_number(double value, uint8_t *buffer, size_t *size) {
    uint8_t *start = buffer;
    write_byte(&buffer, AMF0_NUMBER);
    
    union {
        double d;
        uint64_t u;
    } u;
    u.d = value;
    write_be64(&buffer, u.u);
    
    *size = buffer - start;
    return 1;
}

int rtmp_amf_encode_boolean(int value, uint8_t *buffer, size_t *size) {
    uint8_t *start = buffer;
    write_byte(&buffer, AMF0_BOOLEAN);
    write_byte(&buffer, value ? 1 : 0);
    
    *size = buffer - start;
    return 1;
}

int rtmp_amf_encode_string(const char *str, uint8_t *buffer, size_t *size) {
    size_t str_len = strlen(str);
    if (str_len > AMF_MAX_STRING_LEN) {
        return 0;
    }
    
    uint8_t *start = buffer;
    write_byte(&buffer, AMF0_STRING);
    write_be16(&buffer, str_len);
    memcpy(buffer, str, str_len);
    buffer += str_len;
    
    *size = buffer - start;
    return 1;
}

int rtmp_amf_encode_null(uint8_t *buffer, size_t *size) {
    uint8_t *start = buffer;
    write_byte(&buffer, AMF0_NULL);
    
    *size = buffer - start;
    return 1;
}

int rtmp_amf_encode_undefined(uint8_t *buffer, size_t *size) {
    uint8_t *start = buffer;
    write_byte(&buffer, AMF0_UNDEFINED);
    
    *size = buffer - start;
    return 1;
}

int rtmp_amf_encode_object_start(uint8_t *buffer, size_t *size) {
    uint8_t *start = buffer;
    write_byte(&buffer, AMF0_OBJECT);
    
    *size = buffer - start;
    return 1;
}

int rtmp_amf_encode_object_end(uint8_t *buffer, size_t *size) {
    uint8_t *start = buffer;
    write_be24(&buffer, 0);  // Empty string
    write_byte(&buffer, AMF0_OBJECT_END);
    
    *size = buffer - start;
    return 1;
}

rtmp_amf_value_t* rtmp_amf_value_new(void) {
    return (rtmp_amf_value_t*)calloc(1, sizeof(rtmp_amf_value_t));
}

void rtmp_amf_value_free(rtmp_amf_value_t *value) {
    if (!value) return;
    
    switch (value->type) {
        case AMF0_STRING:
            free(value->value.string.data);
            break;
            
        case AMF0_OBJECT:
            for (uint32_t i = 0; i < value->value.object.size; i++) {
                free(value->value.object.names[i]);
                rtmp_amf_value_free(value->value.object.properties[i]);
            }
            free(value->value.object.names);
            free(value->value.object.properties);
            break;
            
        case AMF0_STRICT_ARRAY:
        case AMF0_ECMA_ARRAY:
            for (uint32_t i = 0; i < value->value.array.size; i++) {
                rtmp_amf_value_free(value->value.array.elements[i]);
            }
            free(value->value.array.elements);
            break;
    }
    
    free(value);
}

int rtmp_amf_encode_connect(const char *app, const char *swf_url, const char *tc_url, uint8_t *buffer, size_t *size) {
    uint8_t *start = buffer;
    size_t tmp_size;
    
    // Command name: "connect"
    rtmp_amf_encode_string("connect", buffer, &tmp_size);
    buffer += tmp_size;
    
    // Transaction ID (always 1 for connect)
    rtmp_amf_encode_number(1.0, buffer, &tmp_size);
    buffer += tmp_size;
    
    // Command object
    rtmp_amf_encode_object_start(buffer, &tmp_size);
    buffer += tmp_size;
    
    // App property
    rtmp_amf_encode_string("app", buffer, &tmp_size);
    buffer += tmp_size;
    rtmp_amf_encode_string(app, buffer, &tmp_size);
    buffer += tmp_size;
    
    // Flash version
    rtmp_amf_encode_string("flashVer", buffer, &tmp_size);
    buffer += tmp_size;
    rtmp_amf_encode_string("WIN 12,0,0,44", buffer, &tmp_size);
    buffer += tmp_size;
    
    // SWF URL
    rtmp_amf_encode_string("swfUrl", buffer, &tmp_size);
    buffer += tmp_size;
    rtmp_amf_encode_string(swf_url, buffer, &tmp_size);
    buffer += tmp_size;
    
    // TC URL
    rtmp_amf_encode_string("tcUrl", buffer, &tmp_size);
    buffer += tmp_size;
    rtmp_amf_encode_string(tc_url, buffer, &tmp_size);
    buffer += tmp_size;
    
    // End object
    rtmp_amf_encode_object_end(buffer, &tmp_size);
    buffer += tmp_size;
    
    *size = buffer - start;
    return 1;
}

int rtmp_amf_encode_create_stream(uint32_t transaction_id, uint8_t *buffer, size_t *size) {
    uint8_t *start = buffer;
    size_t tmp_size;
    
    // Command name
    rtmp_amf_encode_string("createStream", buffer, &tmp_size);
    buffer += tmp_size;
    
    // Transaction ID
    rtmp_amf_encode_number(transaction_id, buffer, &tmp_size);
    buffer += tmp_size;
    
    // Command object (null)
    rtmp_amf_encode_null(buffer, &tmp_size);
    buffer += tmp_size;
    
    *size = buffer - start;
    return 1;
}

int rtmp_amf_encode_play(const char *stream_name, uint8_t *buffer, size_t *size) {
    uint8_t *start = buffer;
    size_t tmp_size;
    
    // Command name
    rtmp_amf_encode_string("play", buffer, &tmp_size);
    buffer += tmp_size;
    
    // Transaction ID (0 for play)
    rtmp_amf_encode_number(0.0, buffer, &tmp_size);
    buffer += tmp_size;
    
    // Command object (null)
    rtmp_amf_encode_null(buffer, &tmp_size);
    buffer += tmp_size;
    
    // Stream name
    rtmp_amf_encode_string(stream_name, buffer, &tmp_size);
    buffer += tmp_size;
    
    *size = buffer - start;
    return 1;
}

int rtmp_amf_encode_publish(const char *stream_name, uint8_t *buffer, size_t *size) {
    uint8_t *start = buffer;
    size_t tmp_size;
    
    // Command name
    rtmp_amf_encode_string("publish", buffer, &tmp_size);
    buffer += tmp_size;
    
    // Transaction ID (0 for publish)
    rtmp_amf_encode_number(0.0, buffer, &tmp_size);
    buffer += tmp_size;
    
    // Command object (null)
    rtmp_amf_encode_null(buffer, &tmp_size);
    buffer += tmp_size;
    
    // Stream name
    rtmp_amf_encode_string(stream_name, buffer, &tmp_size);
    buffer += tmp_size;
    
    // Publish type ("live")
    rtmp_amf_encode_string("live", buffer, &tmp_size);
    buffer += tmp_size;
    
    *size = buffer - start;
    return 1;
}

int rtmp_amf_encode_metadata(const char *name, const uint8_t *data, size_t data_size, uint8_t *buffer, size_t *size) {
    uint8_t *start = buffer;
    size_t tmp_size;
    
    // Command name "@setDataFrame"
    rtmp_amf_encode_string("@setDataFrame", buffer, &tmp_size);
    buffer += tmp_size;
    
    // Metadata type (usually "onMetaData")
    rtmp_amf_encode_string(name, buffer, &tmp_size);
    buffer += tmp_size;
    
    // Copy metadata
    memcpy(buffer, data, data_size);
    buffer += data_size;
    
    *size = buffer - start;
    return 1;
}

int rtmp_amf_decode_number(const uint8_t *buffer, size_t size, double *value, size_t *bytes_read) {
    if (size < AMF_NUMBER_SIZE) return 0;
    
    if (buffer[0] != AMF0_NUMBER) return 0;
    buffer++;
    
    union {
        uint64_t u;
        double d;
    } u;
    u.u = read_be64(&buffer);
    *value = u.d;
    
    *bytes_read = AMF_NUMBER_SIZE;
    return 1;
}

int rtmp_amf_decode_boolean(const uint8_t *buffer, size_t size, int *value, size_t *bytes_read) {
    if (size < AMF_BOOLEAN_SIZE) return 0;
    
    if (buffer[0] != AMF0_BOOLEAN) return 0;
    
    *value = buffer[1] != 0;
    *bytes_read = AMF_BOOLEAN_SIZE;
    return 1;
}

int rtmp_amf_decode_string(const uint8_t *buffer, size_t size, char **str, uint32_t *str_size, size_t *bytes_read) {
    if (size < 3) return 0;  // Type + minimum 2 bytes for length
    
    if (buffer[0] != AMF0_STRING) return 0;
    buffer++;
    
    uint16_t length = read_be16(&buffer);
    if (size < 3 + length) return 0;
    
    *str = (char*)malloc(length + 1);
    if (!*str) return 0;
    
    memcpy(*str, buffer, length);
    (*str)[length] = '\0';
    *str_size = length;
    *bytes_read = 3 + length;
    
    return 1;
}

int rtmp_amf_decode_object(const uint8_t *buffer, size_t size, rtmp_amf_value_t *value, size_t *bytes_read) {
    if (size < 1) return 0;
    
    if (buffer[0] != AMF0_OBJECT) return 0;
    buffer++;
    size--;
    
    value->type = AMF0_OBJECT;
    value->value.object.size = 0;
    value->value.object.properties = NULL;
    value->value.object.names = NULL;
    
    const uint8_t *start = buffer;
    size_t capacity = 16;
    value->value.object.properties = malloc(capacity * sizeof(rtmp_amf_value_t*));
    value->value.object.names = malloc(capacity * sizeof(char*));
    
    if (!value->value.object.properties || !value->value.object.names) {
        free(value->value.object.properties);
        free(value->value.object.names);
        return 0;
    }
    
    while (size >= 3) {  // Minimum size for empty string + type
        // Check for object end marker
        if (buffer[0] == 0 && buffer[1] == 0 && buffer[2] == AMF0_OBJECT_END) {
            buffer += 3;
            break;
        }
        
        // Read property name
        uint16_t name_len = read_be16(&buffer);
        if (size < 2 + name_len) break;
        
        char *name = malloc(name_len + 1);
        if (!name) break;
        
        memcpy(name, buffer, name_len);
        name[name_len] = '\0';
        buffer += name_len;
        size -= 2 + name_len;
        
        // Read property value
        rtmp_amf_value_t *prop = rtmp_amf_value_new();
        if (!prop) {
            free(name);
            break;
        }
        
        size_t tmp_read;
        if (!rtmp_amf_decode_value(buffer, size, prop, &tmp_read)) {
            free(name);
            rtmp_amf_value_free(prop);
            break;
        }
        
        buffer += tmp_read;
        size -= tmp_read;
        
        // Add property
        if (value->value.object.size >= capacity) {
            capacity *= 2;
            void *tmp = realloc(value->value.object.properties, capacity * sizeof(rtmp_amf_value_t*));
            if (!tmp) {
                free(name);
                rtmp_amf_value_free(prop);
                break;
            }
            value->value.object.properties = tmp;
            
            tmp = realloc(value->value.object.names, capacity * sizeof(char*));
            if (!tmp) {
                free(name);
                rtmp_amf_value_free(prop);
                break;
            }
            value->value.object.names = tmp;
        }
        
        value->value.object.names[value->value.object.size] = name;
        value->value.object.properties[value->value.object.size] = prop;
        value->value.object.size++;
    }
    
    *bytes_read = (buffer - start) + 1;  // +1 for type byte
    return 1;
}

static int rtmp_amf_decode_value(const uint8_t *buffer, size_t size, rtmp_amf_value_t *value, size_t *bytes_read) {
    if (size < 1) return 0;
    
    uint8_t type = buffer[0];
    value->type = type;
    
    switch (type) {
        case AMF0_NUMBER:
            return rtmp_amf_decode_number(buffer, size, &value->value.number, bytes_read);
            
        case AMF0_BOOLEAN:
            return rtmp_amf_decode_boolean(buffer, size, &value->value.boolean, bytes_read);
            
        case AMF0_STRING:
            return rtmp_amf_decode_string(buffer, size, &value->value.string.data, &value->value.string.size, bytes_read);
            
        case AMF0_OBJECT:
            return rtmp_amf_decode_object(buffer, size, value, bytes_read);
            
        case AMF0_NULL:
        case AMF0_UNDEFINED:
            *bytes_read = 1;
            return 1;
            
        case AMF0_STRICT_ARRAY:
            return rtmp_amf_decode_array(buffer, size, value, bytes_read);
            
        default:
            return 0;
    }
}

rtmp_amf_value_t* rtmp_amf_value_copy(const rtmp_amf_value_t *value) {
    if (!value) return NULL;
    
    rtmp_amf_value_t *copy = rtmp_amf_value_new();
    if (!copy) return NULL;
    
    copy->type = value->type;
    
    switch (value->type) {
        case AMF0_NUMBER:
            copy->value.number = value->value.number;
            break;
            
        case AMF0_BOOLEAN:
            copy->value.boolean = value->value.boolean;
            break;
            
        case AMF0_STRING:
            copy->value.string.data = strdup(value->value.string.data);
            copy->value.string.size = value->value.string.size;
            if (!copy->value.string.data) {
                rtmp_amf_value_free(copy);
                return NULL;
            }
            break;
            
        case AMF0_OBJECT:
            // Implementação de cópia profunda de objeto
            copy->value.object.size = value->value.object.size;
            copy->value.object.properties = malloc(value->value.object.size * sizeof(rtmp_amf_value_t*));
            copy->value.object.names = malloc(value->value.object.size * sizeof(char*));
            
            if (!copy->value.object.properties || !copy->value.object.names) {
                rtmp_amf_value_free(copy);
                return NULL;
            }
            
            for (uint32_t i = 0; i < value->value.object.size; i++) {
                copy->value.object.names[i] = strdup(value->value.object.names[i]);
                copy->value.object.properties[i] = rtmp_amf_value_copy(value->value.object.properties[i]);
                
                if (!copy->value.object.names[i] || !copy->value.object.properties[i]) {
                    rtmp_amf_value_free(copy);
                    return NULL;
                }
            }
            break;
            
        case AMF0_DATE:
            copy->value.date = value->value.date;
            break;
    }
    
    return copy;
}