#ifndef RTMP_AMF_H
#define RTMP_AMF_H

#include <stdint.h>
#include <stddef.h>

// Tipos AMF0
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

// Estrutura para valores AMF
typedef struct rtmp_amf_value {
    uint8_t type;
    union {
        double number;
        int boolean;
        struct {
            char *data;
            uint32_t size;
        } string;
        struct {
            struct rtmp_amf_value **properties;
            char **names;
            uint32_t size;
        } object;
        struct {
            struct rtmp_amf_value **elements;
            uint32_t size;
        } array;
        double date;
    } value;
} rtmp_amf_value_t;

// Funções de encoding
int rtmp_amf_encode_number(double value, uint8_t *buffer, size_t *size);
int rtmp_amf_encode_boolean(int value, uint8_t *buffer, size_t *size);
int rtmp_amf_encode_string(const char *str, uint8_t *buffer, size_t *size);
int rtmp_amf_encode_null(uint8_t *buffer, size_t *size);
int rtmp_amf_encode_undefined(uint8_t *buffer, size_t *size);
int rtmp_amf_encode_object_start(uint8_t *buffer, size_t *size);
int rtmp_amf_encode_object_end(uint8_t *buffer, size_t *size);
int rtmp_amf_encode_array(rtmp_amf_value_t **elements, uint32_t count, uint8_t *buffer, size_t *size);

// Funções de decoding
int rtmp_amf_decode_number(const uint8_t *buffer, size_t size, double *value, size_t *bytes_read);
int rtmp_amf_decode_boolean(const uint8_t *buffer, size_t size, int *value, size_t *bytes_read);
int rtmp_amf_decode_string(const uint8_t *buffer, size_t size, char **str, uint32_t *str_size, size_t *bytes_read);
int rtmp_amf_decode_object(const uint8_t *buffer, size_t size, rtmp_amf_value_t *value, size_t *bytes_read);
int rtmp_amf_decode_array(const uint8_t *buffer, size_t size, rtmp_amf_value_t *value, size_t *bytes_read);

// Funções de gerenciamento de valores AMF
rtmp_amf_value_t* rtmp_amf_value_new(void);
void rtmp_amf_value_free(rtmp_amf_value_t *value);
rtmp_amf_value_t* rtmp_amf_value_copy(const rtmp_amf_value_t *value);

// Funções para mensagens RTMP específicas
int rtmp_amf_encode_connect(const char *app, const char *swf_url, const char *tc_url, uint8_t *buffer, size_t *size);
int rtmp_amf_encode_create_stream(uint32_t transaction_id, uint8_t *buffer, size_t *size);
int rtmp_amf_encode_play(const char *stream_name, uint8_t *buffer, size_t *size);
int rtmp_amf_encode_publish(const char *stream_name, uint8_t *buffer, size_t *size);
int rtmp_amf_encode_metadata(const char *name, const uint8_t *data, size_t data_size, uint8_t *buffer, size_t *size);

#endif // RTMP_AMF_H