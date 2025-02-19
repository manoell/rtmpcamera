#ifndef RTMP_AMF_H
#define RTMP_AMF_H

#include <stdint.h>
#include <stddef.h>

// AMF0 Types
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

struct AMFObject;

typedef struct AMFValue {
    uint8_t type;
    union {
        double number;
        int boolean;
        char* string;
        struct AMFObject* object;
    } data;
} AMFValue;

typedef struct AMFObject {
    char* name;
    AMFValue* value;
    struct AMFObject* next;
} AMFObject;

// Funções para decodificação
AMFValue* amf_decode(const uint8_t* data, size_t len, size_t* bytes_read);
char* amf_decode_string(const uint8_t* data, size_t len, size_t* bytes_read);
double amf_decode_number(const uint8_t* data, size_t len, size_t* bytes_read);
int amf_decode_boolean(const uint8_t* data, size_t len, size_t* bytes_read);
AMFObject* amf_decode_object(const uint8_t* data, size_t len, size_t* bytes_read);

// Funções para codificação
int amf_encode_string(uint8_t* buffer, size_t len, const char* str, size_t* bytes_written);
int amf_encode_number(uint8_t* buffer, size_t len, double number, size_t* bytes_written);
int amf_encode_boolean(uint8_t* buffer, size_t len, int boolean, size_t* bytes_written);
int amf_encode_object(uint8_t* buffer, size_t len, const AMFObject* obj, size_t* bytes_written);

// Funções de utilidade
void amf_value_free(AMFValue* value);
void amf_object_free(AMFObject* obj);

#endif // RTMP_AMF_H