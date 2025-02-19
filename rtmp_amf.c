#include "rtmp_amf.h"
#include "rtmp_log.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

struct AMFValue {
    uint8_t type;
    union {
        double number;
        int boolean;
        char* string;
        AMFObject* object;
    } data;
};

struct AMFObject {
    char* name;
    AMFValue* value;
    AMFObject* next;
};

static uint16_t read_uint16(const uint8_t* data) {
    return (data[0] << 8) | data[1];
}

static void write_uint16(uint8_t* buffer, uint16_t value) {
    buffer[0] = (value >> 8) & 0xFF;
    buffer[1] = value & 0xFF;
}

char* amf_decode_string(const uint8_t* data, size_t len, size_t* bytes_read) {
    if (len < 2) return NULL;
    
    uint16_t str_len = read_uint16(data);
    data += 2;
    len -= 2;
    
    if (len < str_len) return NULL;
    
    char* str = malloc(str_len + 1);
    if (!str) return NULL;
    
    memcpy(str, data, str_len);
    str[str_len] = '\0';
    
    *bytes_read = str_len + 2;
    return str;
}

double amf_decode_number(const uint8_t* data, size_t len, size_t* bytes_read) {
    if (len < 8) return 0;
    
    union {
        uint64_t i;
        double d;
    } value;
    
    value.i = ((uint64_t)data[0] << 56) |
              ((uint64_t)data[1] << 48) |
              ((uint64_t)data[2] << 40) |
              ((uint64_t)data[3] << 32) |
              ((uint64_t)data[4] << 24) |
              ((uint64_t)data[5] << 16) |
              ((uint64_t)data[6] << 8) |
              data[7];
    
    *bytes_read = 8;
    return value.d;
}

int amf_decode_boolean(const uint8_t* data, size_t len, size_t* bytes_read) {
    if (len < 1) return 0;
    
    *bytes_read = 1;
    return data[0] != 0;
}

AMFObject* amf_decode_object(const uint8_t* data, size_t len, size_t* bytes_read) {
    if (len < 3) return NULL;
    
    AMFObject* obj = NULL;
    AMFObject* last = NULL;
    size_t offset = 0;
    
    while (offset + 3 <= len) {
        if (data[offset] == 0 && data[offset+1] == 0 && data[offset+2] == AMF0_OBJECT_END) {
            offset += 3;
            break;
        }
        
        size_t name_bytes;
        char* name = amf_decode_string(data + offset, len - offset, &name_bytes);
        if (!name) break;
        offset += name_bytes;
        
        size_t value_bytes;
        AMFValue* value = amf_decode(data + offset, len - offset, &value_bytes);
        if (!value) {
            free(name);
            break;
        }
        offset += value_bytes;
        
        AMFObject* new_obj = malloc(sizeof(AMFObject));
        if (!new_obj) {
            free(name);
            amf_value_free(value);
            break;
        }
        
        new_obj->name = name;
        new_obj->value = value;
        new_obj->next = NULL;
        
        if (!obj) {
            obj = new_obj;
        } else {
            last->next = new_obj;
        }
        last = new_obj;
    }
    
    *bytes_read = offset;
    return obj;
}

AMFValue* amf_decode(const uint8_t* data, size_t len, size_t* bytes_read) {
    if (len < 1) return NULL;
    
    uint8_t type = data[0];
    data++;
    len--;
    size_t value_bytes = 0;
    
    AMFValue* value = malloc(sizeof(AMFValue));
    if (!value) return NULL;
    
    value->type = type;
    
    switch (type) {
        case AMF0_NUMBER:
            value->data.number = amf_decode_number(data, len, &value_bytes);
            break;
            
        case AMF0_BOOLEAN:
            value->data.boolean = amf_decode_boolean(data, len, &value_bytes);
            break;
            
        case AMF0_STRING:
            value->data.string = amf_decode_string(data, len, &value_bytes);
            if (!value->data.string) {
                free(value);
                return NULL;
            }
            break;
            
        case AMF0_OBJECT:
            value->data.object = amf_decode_object(data, len, &value_bytes);
            if (!value->data.object) {
                free(value);
                return NULL;
            }
            break;
            
        case AMF0_NULL:
        case AMF0_UNDEFINED:
            value_bytes = 0;
            break;
            
        default:
            LOG_WARNING("Unsupported AMF0 type: %d", type);
            free(value);
            return NULL;
    }
    
    *bytes_read = value_bytes + 1;
    return value;
}

int amf_encode_string(uint8_t* buffer, size_t len, const char* str, size_t* bytes_written) {
    size_t str_len = strlen(str);
    if (len < str_len + 3) return -1;
    
    buffer[0] = AMF0_STRING;
    write_uint16(buffer + 1, str_len);
    memcpy(buffer + 3, str, str_len);
    
    *bytes_written = str_len + 3;
    return 0;
}

int amf_encode_number(uint8_t* buffer, size_t len, double number, size_t* bytes_written) {
    if (len < 9) return -1;
    
    buffer[0] = AMF0_NUMBER;
    union {
        double d;
        uint64_t i;
    } value;
    value.d = number;
    
    buffer[1] = (value.i >> 56) & 0xFF;
    buffer[2] = (value.i >> 48) & 0xFF;
    buffer[3] = (value.i >> 40) & 0xFF;
    buffer[4] = (value.i >> 32) & 0xFF;
    buffer[5] = (value.i >> 24) & 0xFF;
    buffer[6] = (value.i >> 16) & 0xFF;
    buffer[7] = (value.i >> 8) & 0xFF;
    buffer[8] = value.i & 0xFF;
    
    *bytes_written = 9;
    return 0;
}

int amf_encode_boolean(uint8_t* buffer, size_t len, int boolean, size_t* bytes_written) {
    if (len < 2) return -1;
    
    buffer[0] = AMF0_BOOLEAN;
    buffer[1] = boolean ? 1 : 0;
    
    *bytes_written = 2;
    return 0;
}

int amf_encode_object(uint8_t* buffer, size_t len, const AMFObject* obj, size_t* bytes_written) {
    if (!buffer || !obj || len < 4) return -1;
    
    size_t offset = 0;
    buffer[offset++] = AMF0_OBJECT;
    
    while (obj && offset < len) {
        size_t name_len = strlen(obj->name);
        if (offset + 2 + name_len > len) return -1;
        
        write_uint16(buffer + offset, name_len);
        offset += 2;
        memcpy(buffer + offset, obj->name, name_len);
        offset += name_len;
        
        size_t value_bytes;
        switch (obj->value->type) {
            case AMF0_NUMBER:
                if (amf_encode_number(buffer + offset, len - offset, 
                                    obj->value->data.number, &value_bytes) < 0)
                    return -1;
                break;
                
            case AMF0_BOOLEAN:
                if (amf_encode_boolean(buffer + offset, len - offset,
                                     obj->value->data.boolean, &value_bytes) < 0)
                    return -1;
                break;
                
            case AMF0_STRING:
                if (amf_encode_string(buffer + offset, len - offset,
                                    obj->value->data.string, &value_bytes) < 0)
                    return -1;
                break;
                
            default:
                LOG_WARNING("Unsupported AMF0 type for encoding: %d", obj->value->type);
                return -1;
        }
        
        offset += value_bytes;
        obj = obj->next;
    }
    
    if (offset + 3 > len) return -1;
    buffer[offset++] = 0;
    buffer[offset++] = 0;
    buffer[offset++] = AMF0_OBJECT_END;
    
    *bytes_written = offset;
    return 0;
}

void amf_value_free(AMFValue* value) {
    if (!value) return;
    
    switch (value->type) {
        case AMF0_STRING:
            free(value->data.string);
            break;
            
        case AMF0_OBJECT:
            amf_object_free(value->data.object);
            break;
    }
    
    free(value);
}

void amf_object_free(AMFObject* obj) {
    while (obj) {
        AMFObject* next = obj->next;
        free(obj->name);
        amf_value_free(obj->value);
        free(obj);
        obj = next;
    }
}