#include "rtmp_amf.h"
#include <string.h>
#include <arpa/inet.h>

// Helpers
static uint16_t read_uint16(uint8_t* data) {
    return (data[0] << 8) | data[1];
}

static void write_uint16(uint8_t* buffer, uint16_t value) {
    buffer[0] = (value >> 8) & 0xFF;
    buffer[1] = value & 0xFF;
}

// Decode functions
int amf0_decode_string(uint8_t* data, size_t size, char** str, uint16_t* length) {
    if (size < 3) return RTMP_ERROR_PROTOCOL; // Tipo + tamanho

    uint8_t type = data[0];
    if (type != AMF0_STRING && type != AMF0_LONG_STRING) {
        rtmp_log(RTMP_LOG_ERROR, "Expected string type, got %02x", type);
        return RTMP_ERROR_PROTOCOL;
    }

    uint16_t str_len = read_uint16(data + 1);
    if (size < 3 + str_len) return RTMP_ERROR_PROTOCOL;

    *str = malloc(str_len + 1);
    if (!*str) return RTMP_ERROR_MEMORY;

    memcpy(*str, data + 3, str_len);
    (*str)[str_len] = '\0';
    *length = str_len;

    rtmp_log(RTMP_LOG_DEBUG, "Decoded string: %s (len: %d)", *str, str_len);
    return 3 + str_len; // Retorna bytes consumidos
}

int amf0_decode_number(uint8_t* data, size_t size, double* number) {
    if (size < 9) return RTMP_ERROR_PROTOCOL; // Tipo + 8 bytes

    if (data[0] != AMF0_NUMBER) {
        rtmp_log(RTMP_LOG_ERROR, "Expected number type, got %02x", data[0]);
        return RTMP_ERROR_PROTOCOL;
    }

    uint64_t bits = ((uint64_t)data[1] << 56) |
                    ((uint64_t)data[2] << 48) |
                    ((uint64_t)data[3] << 40) |
                    ((uint64_t)data[4] << 32) |
                    ((uint64_t)data[5] << 24) |
                    ((uint64_t)data[6] << 16) |
                    ((uint64_t)data[7] << 8) |
                    data[8];
    memcpy(number, &bits, 8);

    rtmp_log(RTMP_LOG_DEBUG, "Decoded number: %f", *number);
    return 9; // Tipo + 8 bytes
}

int amf0_decode_boolean(uint8_t* data, size_t size, bool* boolean) {
    if (size < 2) return RTMP_ERROR_PROTOCOL; // Tipo + valor

    if (data[0] != AMF0_BOOLEAN) {
        rtmp_log(RTMP_LOG_ERROR, "Expected boolean type, got %02x", data[0]);
        return RTMP_ERROR_PROTOCOL;
    }

    *boolean = data[1] != 0;
    rtmp_log(RTMP_LOG_DEBUG, "Decoded boolean: %d", *boolean);
    return 2;
}

int amf0_decode_null(uint8_t* data, size_t size) {
    if (size < 1) return RTMP_ERROR_PROTOCOL;

    if (data[0] != AMF0_NULL) {
        rtmp_log(RTMP_LOG_ERROR, "Expected null type, got %02x", data[0]);
        return RTMP_ERROR_PROTOCOL;
    }

    rtmp_log(RTMP_LOG_DEBUG, "Decoded null");
    return 1;
}

// Encode functions
int amf0_encode_string(char* str, uint8_t* buffer, size_t* size) {
    uint16_t str_len = strlen(str);
    size_t needed = 3 + str_len; // Tipo + tamanho + string

    if (buffer && *size >= needed) {
        buffer[0] = AMF0_STRING;
        write_uint16(buffer + 1, str_len);
        memcpy(buffer + 3, str, str_len);
    }
    
    *size = needed;
    return RTMP_OK;
}

int amf0_encode_number(double number, uint8_t* buffer, size_t* size) {
    size_t needed = 9; // Tipo + 8 bytes
    
    if (buffer && *size >= needed) {
        buffer[0] = AMF0_NUMBER;
        uint64_t bits;
        memcpy(&bits, &number, 8);
        
        // Big endian
        buffer[1] = (bits >> 56) & 0xFF;
        buffer[2] = (bits >> 48) & 0xFF;
        buffer[3] = (bits >> 40) & 0xFF;
        buffer[4] = (bits >> 32) & 0xFF;
        buffer[5] = (bits >> 24) & 0xFF;
        buffer[6] = (bits >> 16) & 0xFF;
        buffer[7] = (bits >> 8) & 0xFF;
        buffer[8] = bits & 0xFF;
    }
    
    *size = needed;
    return RTMP_OK;
}

int amf0_encode_boolean(bool boolean, uint8_t* buffer, size_t* size) {
    size_t needed = 2; // Tipo + valor
    
    if (buffer && *size >= needed) {
        buffer[0] = AMF0_BOOLEAN;
        buffer[1] = boolean ? 1 : 0;
    }
    
    *size = needed;
    return RTMP_OK;
}

int amf0_encode_null(uint8_t* buffer, size_t* size) {
    size_t needed = 1; // Só tipo
    
    if (buffer && *size >= needed) {
        buffer[0] = AMF0_NULL;
    }
    
    *size = needed;
    return RTMP_OK;
}

void amf_value_free(AMFValue* value) {
    if (!value) return;
    
    switch (value->type) {
        case AMF0_STRING:
            free(value->value.string.data);
            break;
        case AMF0_OBJECT:
            // Limpar objeto recursivamente
            if (value->value.object) {
                free(value->value.object->name);
                amf_value_free(value->value.object->value);
                free(value->value.object);
            }
            break;
    }
}