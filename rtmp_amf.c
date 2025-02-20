#include "rtmp_amf.h"
#include "rtmp_log.h"
#include <stdlib.h>
#include <string.h>

// Funções auxiliares privadas
static uint16_t read_uint16(const uint8_t* data) {
    return (data[0] << 8) | data[1];
}

static uint32_t read_uint32(const uint8_t* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

static void write_uint16(uint8_t* buffer, uint16_t value) {
    buffer[0] = (value >> 8) & 0xFF;
    buffer[1] = value & 0xFF;
}

static void write_uint32(uint8_t* buffer, uint32_t value) {
    buffer[0] = (value >> 24) & 0xFF;
    buffer[1] = (value >> 16) & 0xFF;
    buffer[2] = (value >> 8) & 0xFF;
    buffer[3] = value & 0xFF;
}

// Funções de decodificação
char* amf_decode_string(const uint8_t* data, size_t len, size_t* bytes_read) {
    if (len < 2) {
        *bytes_read = 0;
        return NULL;
    }
    
    uint16_t str_len = read_uint16(data);
    if (len < 2 + str_len) {
        *bytes_read = 0;
        return NULL;
    }
    
    char* str = malloc(str_len + 1);
    if (!str) {
        *bytes_read = 0;
        return NULL;
    }
    
    memcpy(str, data + 2, str_len);
    str[str_len] = '\0';
    *bytes_read = 2 + str_len;
    
    return str;
}

double amf_decode_number(const uint8_t* data, size_t len, size_t* bytes_read) {
    if (len < 8) {
        *bytes_read = 0;
        return 0;
    }
    
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
    if (len < 1) {
        *bytes_read = 0;
        return 0;
    }
    
    *bytes_read = 1;
    return data[0] != 0;
}

AMFObject* amf_decode_object(const uint8_t* data, size_t len, size_t* bytes_read) {
    if (len < 3) {
        *bytes_read = 0;
        return NULL;
    }
    
    size_t total_bytes = 0;
    AMFObject* first = NULL;
    AMFObject* last = NULL;
    
    while (total_bytes + 3 <= len) {
        // Verificar fim do objeto
        if (data[total_bytes] == 0 && 
            data[total_bytes + 1] == 0 && 
            data[total_bytes + 2] == AMF0_OBJECT_END) {
            total_bytes += 3;
            break;
        }
        
        // Ler nome da propriedade
        size_t name_bytes;
        char* name = amf_decode_string(data + total_bytes, len - total_bytes, &name_bytes);
        if (!name) break;
        total_bytes += name_bytes;
        
        // Ler valor
        size_t value_bytes;
        AMFValue* value = amf_decode(data + total_bytes, len - total_bytes, &value_bytes);
        if (!value) {
            free(name);
            break;
        }
        total_bytes += value_bytes;
        
        // Criar objeto
        AMFObject* obj = amf_object_create(name, value);
        free(name); // amf_object_create faz cópia
        
        if (!obj) {
            amf_value_free(value);
            break;
        }
        
        // Adicionar à lista
        if (!first) {
            first = obj;
        } else {
            last->next = obj;
        }
        last = obj;
    }
    
    *bytes_read = total_bytes;
    return first;
}

AMFValue* amf_decode(const uint8_t* data, size_t len, size_t* bytes_read) {
    if (len < 1) {
        *bytes_read = 0;
        return NULL;
    }
    
    uint8_t type = data[0];
    size_t value_bytes = 0;
    AMFValue* value = NULL;
    
    switch (type) {
        case AMF0_NUMBER:
            if (len < 9) break;
            value = amf_value_create_number(amf_decode_number(data + 1, len - 1, &value_bytes));
            break;
            
        case AMF0_BOOLEAN:
            if (len < 2) break;
            value = amf_value_create_boolean(amf_decode_boolean(data + 1, len - 1, &value_bytes));
            break;
            
        case AMF0_STRING:
            {
                char* str = amf_decode_string(data + 1, len - 1, &value_bytes);
                if (str) {
                    value = amf_value_create_string(str);
                    free(str);
                }
            }
            break;
            
        case AMF0_OBJECT:
            {
                AMFObject* obj = amf_decode_object(data + 1, len - 1, &value_bytes);
                if (obj) {
                    value = malloc(sizeof(AMFValue));
                    if (value) {
                        value->type = AMF0_OBJECT;
                        value->data.object = obj;
                    } else {
                        amf_object_free(obj);
                    }
                }
            }
            break;
            
        case AMF0_NULL:
        case AMF0_UNDEFINED:
            value = amf_value_create_null();
            value_bytes = 0;
            break;
            
        default:
            LOG_WARNING("Unsupported AMF0 type: %d", type);
            break;
    }
    
    if (value) {
        *bytes_read = value_bytes + 1;
    } else {
        *bytes_read = 0;
    }
    
    return value;
}

// Funções de codificação
int amf_encode_string(uint8_t* buffer, size_t len, const char* str, size_t* bytes_written) {
    size_t str_len = strlen(str);
    if (len < str_len + 3) {
        *bytes_written = 0;
        return -1;
    }
    
    buffer[0] = AMF0_STRING;
    write_uint16(buffer + 1, str_len);
    memcpy(buffer + 3, str, str_len);
    
    *bytes_written = str_len + 3;
    return 0;
}

int amf_encode_number(uint8_t* buffer, size_t len, double number, size_t* bytes_written) {
    if (len < 9) {
        *bytes_written = 0;
        return -1;
    }
    
    buffer[0] = AMF0_NUMBER;
    
    union {
        double d;
        uint64_t i;
    } value;
    value.d = number;
    
    for (int i = 0; i < 8; i++) {
        buffer[1 + i] = (value.i >> (56 - i * 8)) & 0xFF;
    }
    
    *bytes_written = 9;
    return 0;
}

int amf_encode_boolean(uint8_t* buffer, size_t len, int boolean, size_t* bytes_written) {
    if (len < 2) {
        *bytes_written = 0;
        return -1;
    }
    
    buffer[0] = AMF0_BOOLEAN;
    buffer[1] = boolean ? 1 : 0;
    
    *bytes_written = 2;
    return 0;
}

int amf_encode_null(uint8_t* buffer, size_t len, size_t* bytes_written) {
    if (len < 1) {
        *bytes_written = 0;
        return -1;
    }
    
    buffer[0] = AMF0_NULL;
    *bytes_written = 1;
    return 0;
}

int amf_encode_object(uint8_t* buffer, size_t len, const AMFObject* obj, size_t* bytes_written) {
    if (!buffer || !obj || len < 4) {
        *bytes_written = 0;
        return -1;
    }
    
    size_t offset = 0;
    buffer[offset++] = AMF0_OBJECT;
    
    while (obj && offset < len) {
        // Encode property name
        size_t name_len = strlen(obj->name);
        if (offset + 2 + name_len > len) {
            *bytes_written = 0;
            return -1;
        }
        
        write_uint16(buffer + offset, name_len);
        offset += 2;
        memcpy(buffer + offset, obj->name, name_len);
        offset += name_len;
        
        // Encode property value
        size_t value_bytes;
        if (amf_encode_value(obj->value, buffer + offset, len - offset, &value_bytes) < 0) {
            *bytes_written = 0;
            return -1;
        }
        offset += value_bytes;
        
        obj = obj->next;
    }
    
    // Write object end marker
    if (offset + 3 > len) {
        *bytes_written = 0;
        return -1;
    }
    
    buffer[offset++] = 0;
    buffer[offset++] = 0;
    buffer[offset++] = AMF0_OBJECT_END;
    
    *bytes_written = offset;
    return 0;
}

int amf_encode_value(const AMFValue* value, uint8_t* buffer, size_t len, size_t* bytes_written) {
    if (!value || !buffer || !len) {
        *bytes_written = 0;
        return -1;
    }
    
    switch (value->type) {
        case AMF0_NUMBER:
            return amf_encode_number(buffer, len, value->data.number, bytes_written);
            
        case AMF0_BOOLEAN:
            return amf_encode_boolean(buffer, len, value->data.boolean, bytes_written);
            
        case AMF0_STRING:
            return amf_encode_string(buffer, len, value->data.string, bytes_written);
            
        case AMF0_OBJECT:
            return amf_encode_object(buffer, len, value->data.object, bytes_written);
            
        case AMF0_NULL:
        case AMF0_UNDEFINED:
            return amf_encode_null(buffer, len, bytes_written);
            
        default:
            LOG_WARNING("Unsupported AMF0 type for encoding: %d", value->type);
            *bytes_written = 0;
            return -1;
    }
}

// Funções de utilidade
AMFValue* amf_value_create_string(const char* str) {
    AMFValue* value = malloc(sizeof(AMFValue));
    if (!value) return NULL;
    
    value->type = AMF0_STRING;
    value->data.string = strdup(str);
    
    if (!value->data.string) {
        free(value);
        return NULL;
    }
    
    return value;
}

AMFValue* amf_value_create_number(double num) {
    AMFValue* value = malloc(sizeof(AMFValue));
    if (!value) return NULL;
    
    value->type = AMF0_NUMBER;
    value->data.number = num;
    
    return value;
}

AMFValue* amf_value_create_boolean(int boolean) {
    AMFValue* value = malloc(sizeof(AMFValue));
    if (!value) return NULL;
    
    value->type = AMF0_BOOLEAN;
    value->data.boolean = boolean;
    
    return value;
}

AMFValue* amf_value_create_null(void) {
    AMFValue* value = malloc(sizeof(AMFValue));
    if (!value) return NULL;
    
    value->type = AMF0_NULL;
    return value;
}

AMFObject* amf_object_create(const char* name, AMFValue* value) {
    AMFObject* obj = malloc(sizeof(AMFObject));
    if (!obj) return NULL;
    
    obj->name = strdup(name);
    if (!obj->name) {
        free(obj);
        return NULL;
    }
    
    obj->value = value;
    obj->next = NULL;
    
    return obj;
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

// Funções específicas para RTMP
int amf_encode_connect_response(uint8_t* buffer, size_t len, size_t* bytes_written) {
    // Criar objeto de resposta
    AMFObject* properties = NULL;
    AMFObject* obj = NULL;
    size_t offset = 0;
    
    // "_result"
    if (amf_encode_string(buffer, len, RTMP_CMD_RESULT, &offset) < 0) goto error;
    
    // Transaction ID (1)
    size_t bytes;
    if (amf_encode_number(buffer + offset, len - offset, 1.0, &bytes) < 0) goto error;
    offset += bytes;
    
    // Properties object
    properties = amf_object_create("fmsVer", amf_value_create_string("FMS/3,0,1,123"));
    if (!properties) goto error;
    
    obj = amf_object_create("capabilities", amf_value_create_number(31.0));
    if (!obj) goto error;
    properties->next = obj;
    
    if (amf_encode_object(buffer + offset, len - offset, properties, &bytes) < 0) goto error;
    offset += bytes;
    
    // Information object
    AMFObject* info = amf_object_create("level", amf_value_create_string("status"));
    if (!info) goto error;
    
    obj = amf_object_create("code", amf_value_create_string("NetConnection.Connect.Success"));
    if (!obj) goto error;
    info->next = obj;
    
    obj = amf_object_create("description", amf_value_create_string("Connection succeeded."));
    if (!obj) goto error;
    info->next->next = obj;
    
    if (amf_encode_object(buffer + offset, len - offset, info, &bytes) < 0) goto error;
    offset += bytes;
    
    *bytes_written = offset;
    amf_object_free(properties);
    amf_object_free(info);
    return 0;
    
error:
    if (properties) amf_object_free(properties);
    if (info) amf_object_free(info);
    *bytes_written = 0;
    return -1;
}

int amf_encode_create_stream_response(uint8_t* buffer, size_t len, double transaction_id, uint32_t stream_id, size_t* bytes_written) {
    size_t offset = 0;
    
    // "_result"
    if (amf_encode_string(buffer, len, RTMP_CMD_RESULT, &offset) < 0) return -1;
    
    // Transaction ID
    size_t bytes;
    if (amf_encode_number(buffer + offset, len - offset, transaction_id, &bytes) < 0) return -1;
    offset += bytes;
    
    // NULL object
    if (amf_encode_null(buffer + offset, len - offset, &bytes) < 0) return -1;
    offset += bytes;
    
    // Stream ID
    if (amf_encode_number(buffer + offset, len - offset, stream_id, &bytes) < 0) return -1;
    offset += bytes;
    
    *bytes_written = offset;
    return 0;
}

int amf_encode_play_response(uint8_t* buffer, size_t len, const char* stream_name, size_t* bytes_written) {
    // Criar objeto de resposta
    AMFObject* info = amf_object_create("level", amf_value_create_string("status"));
    if (!info) return -1;
    
    AMFObject* obj = amf_object_create("code", amf_value_create_string("NetStream.Play.Start"));
    if (!obj) {
        amf_object_free(info);
        return -1;
    }
    info->next = obj;
    
    char desc[256];
    snprintf(desc, sizeof(desc), "Started playing %s.", stream_name);
    obj = amf_object_create("description", amf_value_create_string(desc));
    if (!obj) {
        amf_object_free(info);
        return -1;
    }
    info->next->next = obj;
    
    // Codificar objeto
    size_t offset = 0;
    
    // "onStatus"
    if (amf_encode_string(buffer, len, RTMP_CMD_ON_STATUS, &offset) < 0) {
        amf_object_free(info);
        return -1;
    }
    
    // Transaction ID (0)
    size_t bytes;
    if (amf_encode_number(buffer + offset, len - offset, 0.0, &bytes) < 0) {
        amf_object_free(info);
        return -1;
    }
    offset += bytes;
    
    // NULL object
    if (amf_encode_null(buffer + offset, len - offset, &bytes) < 0) {
        amf_object_free(info);
        return -1;
    }
    offset += bytes;
    
    // Info object
    if (amf_encode_object(buffer + offset, len - offset, info, &bytes) < 0) {
        amf_object_free(info);
        return -1;
    }
    offset += bytes;
    
    *bytes_written = offset;
    amf_object_free(info);
    return 0;
}

int amf_encode_publish_response(uint8_t* buffer, size_t len, const char* stream_name, size_t* bytes_written) {
    // Criar objeto de resposta
    AMFObject* info = amf_object_create("level", amf_value_create_string("status"));
    if (!info) return -1;
    
    AMFObject* obj = amf_object_create("code", amf_value_create_string("NetStream.Publish.Start"));
    if (!obj) {
        amf_object_free(info);
        return -1;
    }
    info->next = obj;
    
    char desc[256];
    snprintf(desc, sizeof(desc), "Started publishing %s.", stream_name);
    obj = amf_object_create("description", amf_value_create_string(desc));
    if (!obj) {
        amf_object_free(info);
        return -1;
    }
    info->next->next = obj;
    
    // Codificar objeto
    size_t offset = 0;
    
    // "onStatus"
    if (amf_encode_string(buffer, len, RTMP_CMD_ON_STATUS, &offset) < 0) {
        amf_object_free(info);
        return -1;
    }
    
    // Transaction ID (0)
    size_t bytes;
    if (amf_encode_number(buffer + offset, len - offset, 0.0, &bytes) < 0) {
        amf_object_free(info);
        return -1;
    }
    offset += bytes;
    
    // NULL object
    if (amf_encode_null(buffer + offset, len - offset, &bytes) < 0) {
        amf_object_free(info);
        return -1;
    }
    offset += bytes;
    
    // Info object
    if (amf_encode_object(buffer + offset, len - offset, info, &bytes) < 0) {
        amf_object_free(info);
        return -1;
    }
    offset += bytes;
    
    *bytes_written = offset;
    amf_object_free(info);
    return 0;
}

int amf_encode_error(uint8_t* buffer, size_t len, double transaction_id, const char* error_msg, size_t* bytes_written) {
    size_t offset = 0;
    
    // "_error"
    if (amf_encode_string(buffer, len, RTMP_CMD_ERROR, &offset) < 0) return -1;
    
    // Transaction ID
    size_t bytes;
    if (amf_encode_number(buffer + offset, len - offset, transaction_id, &bytes) < 0) return -1;
    offset += bytes;
    
    // NULL object
    if (amf_encode_null(buffer + offset, len - offset, &bytes) < 0) return -1;
    offset += bytes;
    
    // Error object
    AMFObject* error = amf_object_create("level", amf_value_create_string("error"));
    if (!error) return -1;
    
    AMFObject* obj = amf_object_create("code", amf_value_create_string("NetConnection.Error"));
    if (!obj) {
        amf_object_free(error);
        return -1;
    }
    error->next = obj;
    
    obj = amf_object_create("description", amf_value_create_string(error_msg));
    if (!obj) {
        amf_object_free(error);
        return -1;
    }
    error->next->next = obj;
    
    if (amf_encode_object(buffer + offset, len - offset, error, &bytes) < 0) {
        amf_object_free(error);
        return -1;
    }
    offset += bytes;
    
    *bytes_written = offset;
    amf_object_free(error);
    return 0;
}