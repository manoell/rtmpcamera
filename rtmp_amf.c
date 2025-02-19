#include <stdio.h>
#include <string.h>
#include "rtmp_amf.h"
#include "rtmp_log.h"

int amf_decode_string(const uint8_t *data, size_t data_size, char *string, size_t string_size) {
    if (!data || data_size < 3 || !string) {
        return -1;
    }
    
    if (data[0] != AMF0_STRING) {
        log_rtmp_level(RTMP_LOG_ERROR, "AMF string marker inválido: %02x", data[0]);
        return -1;
    }
    
    uint16_t str_len = (data[1] << 8) | data[2];
    if (str_len + 3 > data_size) {
        log_rtmp_level(RTMP_LOG_ERROR, "Tamanho da string excede o buffer");
        return -1;
    }
    
    size_t copy_len = (str_len < string_size - 1) ? str_len : string_size - 1;
    memcpy(string, data + 3, copy_len);
    string[copy_len] = '\0';
    
    return str_len + 3;
}

int amf_decode_number(const uint8_t *data, size_t data_size, double *number) {
    if (!data || data_size < 9 || !number) {
        return -1;
    }
    
    if (data[0] != AMF0_NUMBER) {
        log_rtmp_level(RTMP_LOG_ERROR, "AMF number marker inválido: %02x", data[0]);
        return -1;
    }
    
    // Extrair número (big-endian IEEE-754)
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value = (value << 8) | data[i + 1];
    }
    
    memcpy(number, &value, 8);
    return 9;
}

int amf_encode_string(uint8_t *data, size_t data_size, const char *string) {
    if (!data || !string || data_size < 3) {
        return -1;
    }
    
    size_t str_len = strlen(string);
    if (str_len > 0xFFFF) {
        log_rtmp_level(RTMP_LOG_ERROR, "String muito longa para AMF");
        return -1;
    }
    
    if (str_len + 3 > data_size) {
        log_rtmp_level(RTMP_LOG_ERROR, "Buffer insuficiente para AMF string");
        return -1;
    }
    
    data[0] = AMF0_STRING;
    data[1] = (str_len >> 8) & 0xFF;
    data[2] = str_len & 0xFF;
    memcpy(data + 3, string, str_len);
    
    return str_len + 3;
}

int amf_encode_number(uint8_t *data, size_t data_size, double number) {
    if (!data || data_size < 9) {
        return -1;
    }
    
    data[0] = AMF0_NUMBER;
    
    // Armazenar número em IEEE-754 big-endian
    uint64_t value;
    memcpy(&value, &number, 8);
    
    for (int i = 0; i < 8; i++) {
        data[1 + i] = (value >> (56 - i * 8)) & 0xFF;
    }
    
    return 9;
}

int amf_encode_boolean(uint8_t *data, size_t data_size, int boolean) {
    if (!data || data_size < 2) {
        return -1;
    }
    
    data[0] = AMF0_BOOLEAN;
    data[1] = boolean ? 1 : 0;
    
    return 2;
}

int amf_encode_null(uint8_t *data, size_t data_size) {
    if (!data || data_size < 1) {
        return -1;
    }
    
    data[0] = AMF0_NULL;
    return 1;
}

int amf_encode_object_start(uint8_t *data, size_t data_size) {
    if (!data || data_size < 1) {
        return -1;
    }
    
    data[0] = AMF0_OBJECT;
    return 1;
}

int amf_encode_object_end(uint8_t *data, size_t data_size) {
    if (!data || data_size < 3) {
        return -1;
    }
    
    data[0] = 0x00;
    data[1] = 0x00;
    data[2] = AMF0_OBJECT_END;
    
    return 3;
}