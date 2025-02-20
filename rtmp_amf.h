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

// RTMP Command Names
#define RTMP_CMD_CONNECT          "connect"
#define RTMP_CMD_CREATE_STREAM    "createStream"
#define RTMP_CMD_PLAY             "play"
#define RTMP_CMD_PLAY2            "play2"
#define RTMP_CMD_DELETE_STREAM    "deleteStream"
#define RTMP_CMD_CLOSE_STREAM     "closeStream"
#define RTMP_CMD_PUBLISH          "publish"
#define RTMP_CMD_SEEK            "seek"
#define RTMP_CMD_PAUSE           "pause"
#define RTMP_CMD_ON_STATUS       "onStatus"
#define RTMP_CMD_RESULT          "_result"
#define RTMP_CMD_ERROR           "_error"

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

// Funções de decodificação
AMFValue* amf_decode(const uint8_t* data, size_t len, size_t* bytes_read);
char* amf_decode_string(const uint8_t* data, size_t len, size_t* bytes_read);
double amf_decode_number(const uint8_t* data, size_t len, size_t* bytes_read);
int amf_decode_boolean(const uint8_t* data, size_t len, size_t* bytes_read);
AMFObject* amf_decode_object(const uint8_t* data, size_t len, size_t* bytes_read);

// Funções de codificação
int amf_encode_value(const AMFValue* value, uint8_t* buffer, size_t len, size_t* bytes_written);
int amf_encode_string(uint8_t* buffer, size_t len, const char* str, size_t* bytes_written);
int amf_encode_number(uint8_t* buffer, size_t len, double number, size_t* bytes_written);
int amf_encode_boolean(uint8_t* buffer, size_t len, int boolean, size_t* bytes_written);
int amf_encode_object(uint8_t* buffer, size_t len, const AMFObject* obj, size_t* bytes_written);
int amf_encode_null(uint8_t* buffer, size_t len, size_t* bytes_written);

// Funções de utilidade
void amf_value_free(AMFValue* value);
void amf_object_free(AMFObject* obj);
AMFValue* amf_value_create_string(const char* str);
AMFValue* amf_value_create_number(double num);
AMFValue* amf_value_create_boolean(int boolean);
AMFValue* amf_value_create_null(void);
AMFObject* amf_object_create(const char* name, AMFValue* value);

// Funções específicas para RTMP
int amf_encode_connect_response(uint8_t* buffer, size_t len, size_t* bytes_written);
int amf_encode_create_stream_response(uint8_t* buffer, size_t len, double transaction_id, uint32_t stream_id, size_t* bytes_written);
int amf_encode_play_response(uint8_t* buffer, size_t len, const char* stream_name, size_t* bytes_written);
int amf_encode_publish_response(uint8_t* buffer, size_t len, const char* stream_name, size_t* bytes_written);
int amf_encode_error(uint8_t* buffer, size_t len, double transaction_id, const char* error_msg, size_t* bytes_written);

#endif // RTMP_AMF_H