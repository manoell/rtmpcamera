#ifndef RTMP_AMF_H
#define RTMP_AMF_H

#include <stdint.h>
#include <stdlib.h>

// AMF0 Markers
#define AMF0_NUMBER      0x00
#define AMF0_BOOLEAN     0x01
#define AMF0_STRING      0x02
#define AMF0_OBJECT      0x03
#define AMF0_NULL        0x05
#define AMF0_ECMA_ARRAY  0x08
#define AMF0_OBJECT_END  0x09

// AMF functions
int amf_decode_string(const uint8_t *data, size_t data_size, char *string, size_t string_size);
int amf_decode_number(const uint8_t *data, size_t data_size, double *number);
int amf_encode_string(uint8_t *data, size_t data_size, const char *string);
int amf_encode_number(uint8_t *data, size_t data_size, double number);
int amf_encode_boolean(uint8_t *data, size_t data_size, int boolean);
int amf_encode_null(uint8_t *data, size_t data_size);
int amf_encode_object_start(uint8_t *data, size_t data_size);
int amf_encode_object_end(uint8_t *data, size_t data_size);

#endif /* RTMP_AMF_H */