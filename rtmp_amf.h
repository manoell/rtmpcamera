#ifndef RTMP_AMF_H
#define RTMP_AMF_H

#include <stdint.h>
#include <stddef.h>

typedef struct rtmp_amf_s rtmp_amf_t;

// Creation and destruction
rtmp_amf_t* rtmp_amf_create(void);
void rtmp_amf_destroy(rtmp_amf_t *amf);

// Encoding functions
int rtmp_amf_encode_number(rtmp_amf_t *amf, double value);
int rtmp_amf_encode_boolean(rtmp_amf_t *amf, int value);
int rtmp_amf_encode_string(rtmp_amf_t *amf, const char *str);
int rtmp_amf_encode_null(rtmp_amf_t *amf);
int rtmp_amf_begin_object(rtmp_amf_t *amf);
int rtmp_amf_end_object(rtmp_amf_t *amf);

// Decoding functions
int rtmp_amf_decode_number(const uint8_t *data, size_t size, size_t *offset, double *value);
int rtmp_amf_decode_boolean(const uint8_t *data, size_t size, size_t *offset, int *value);
int rtmp_amf_decode_string(const uint8_t *data, size_t size, size_t *offset, char **str);

// Buffer access
const uint8_t* rtmp_amf_get_data(rtmp_amf_t *amf, size_t *size);

#endif // RTMP_AMF_H