// rtmp_amf.h
#ifndef RTMP_AMF_H
#define RTMP_AMF_H

#include "rtmp_core.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
    size_t position;
} RTMPBuffer;

// Funções do buffer
RTMPBuffer* rtmp_buffer_create(size_t initial_size);
void rtmp_buffer_destroy(RTMPBuffer* buffer);
int rtmp_buffer_ensure_space(RTMPBuffer* buffer, size_t additional);

// Funções de encoding
int rtmp_amf0_write_string(RTMPBuffer* buffer, const char* str);
int rtmp_amf0_write_number(RTMPBuffer* buffer, double number);
int rtmp_amf0_write_boolean(RTMPBuffer* buffer, uint8_t boolean);
int rtmp_amf0_write_null(RTMPBuffer* buffer);
int rtmp_amf0_write_object_start(RTMPBuffer* buffer);
int rtmp_amf0_write_object_end(RTMPBuffer* buffer);

// Funções de decoding
int rtmp_amf0_read_string(RTMPBuffer* buffer, char** str, uint16_t* len);
int rtmp_amf0_read_number(RTMPBuffer* buffer, double* number);
int rtmp_amf0_read_boolean(RTMPBuffer* buffer, uint8_t* boolean);
int rtmp_amf0_read_object_start(RTMPBuffer* buffer);
int rtmp_amf0_read_object_end(RTMPBuffer* buffer);

#ifdef __cplusplus
}
#endif

#endif