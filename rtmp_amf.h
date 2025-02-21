#ifndef RTMP_AMF_H
#define RTMP_AMF_H

#include "rtmp_core.h"

// Tipos AMF0
#define AMF0_NUMBER      0x00
#define AMF0_BOOLEAN     0x01
#define AMF0_STRING      0x02
#define AMF0_OBJECT      0x03
#define AMF0_MOVIECLIP   0x04
#define AMF0_NULL        0x05
#define AMF0_UNDEFINED   0x06
#define AMF0_REFERENCE   0x07
#define AMF0_ECMA_ARRAY  0x08
#define AMF0_OBJECT_END  0x09
#define AMF0_STRICT_ARRAY 0x0A
#define AMF0_DATE        0x0B
#define AMF0_LONG_STRING 0x0C

// Estruturas para valores AMF
typedef struct {
    uint8_t type;
    union {
        double number;
        bool boolean;
        struct {
            char* data;
            uint16_t length;
        } string;
        struct {
            char* name;
            void* value;
        }* object;
    } value;
} AMFValue;

// Funções de decode
int amf0_decode_string(uint8_t* data, size_t size, char** str, uint16_t* length);
int amf0_decode_number(uint8_t* data, size_t size, double* number);
int amf0_decode_boolean(uint8_t* data, size_t size, bool* boolean);
int amf0_decode_null(uint8_t* data, size_t size);

// Funções de encode
int amf0_encode_string(char* str, uint8_t* buffer, size_t* size);
int amf0_encode_number(double number, uint8_t* buffer, size_t* size);
int amf0_encode_boolean(bool boolean, uint8_t* buffer, size_t* size);
int amf0_encode_null(uint8_t* buffer, size_t* size);

// Função para liberar recursos
void amf_value_free(AMFValue* value);

#endif