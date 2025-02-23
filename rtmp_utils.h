#ifndef RTMP_UTILS_H
#define RTMP_UTILS_H

#include <stdint.h>
#include <stdbool.h>

// Buffer management
uint8_t *rtmp_buffer_create(size_t size);
void rtmp_buffer_free(uint8_t *buffer);
void rtmp_buffer_reset(uint8_t *buffer);
size_t rtmp_buffer_size(uint8_t *buffer);

// Network utilities
bool rtmp_is_network_reachable(void);
uint32_t rtmp_get_timestamp(void);
void rtmp_sleep_ms(uint32_t ms);

// RTMP packet utilities
typedef struct RTMPPacket {
    uint8_t *data;
    size_t size;
    uint32_t timestamp;
    uint8_t type;
    uint32_t streamId;
} RTMPPacket;

RTMPPacket *rtmp_packet_create(void);
void rtmp_packet_free(RTMPPacket *packet);
RTMPPacket *rtmp_packet_copy(const RTMPPacket *packet);
bool rtmp_packet_alloc(RTMPPacket *packet, size_t size);

// Logging utilities
typedef enum {
    RTMP_LOG_ERROR = 0,
    RTMP_LOG_WARNING,
    RTMP_LOG_INFO,
    RTMP_LOG_DEBUG
} RTMPLogLevel;

void rtmp_log(RTMPLogLevel level, const char *format, ...);
void rtmp_set_log_level(RTMPLogLevel level);
RTMPLogLevel rtmp_get_log_level(void);

// AMF utilities
typedef enum {
    AMF_NUMBER = 0,
    AMF_BOOLEAN,
    AMF_STRING,
    AMF_OBJECT,
    AMF_NULL,
    AMF_UNDEFINED,
    AMF_REFERENCE,
    AMF_ECMA_ARRAY,
    AMF_OBJECT_END,
    AMF_STRICT_ARRAY,
    AMF_DATE,
    AMF_LONG_STRING,
    AMF_UNSUPPORTED,
    AMF_XML_DOCUMENT,
    AMF_TYPED_OBJECT,
    AMF_AVMPLUS_OBJECT
} AMFDataType;

typedef struct AMFObject AMFObject;
typedef struct AMFObjectProperty AMFObjectProperty;

AMFObject *amf_object_create(void);
void amf_object_free(AMFObject *obj);
int amf_encode_string(char *output, const char *str);
int amf_encode_number(char *output, double number);
int amf_encode_boolean(char *output, bool boolean);
size_t amf_decode_string(const char *input, char **str);
size_t amf_decode_number(const char *input, double *number);
size_t amf_decode_boolean(const char *input, bool *boolean);

// Performance monitoring
void rtmp_perf_start(const char *name);
void rtmp_perf_end(const char *name);
void rtmp_perf_reset(void);
void rtmp_perf_print_stats(void);

// Error handling
typedef enum {
    RTMP_ERROR_NONE = 0,
    RTMP_ERROR_SOCKET_CREATE_FAILED,
    RTMP_ERROR_SOCKET_CONNECT_FAILED,
    RTMP_ERROR_HANDSHAKE_FAILED,
    RTMP_ERROR_PUBLISH_FAILED,
    RTMP_ERROR_WRITE_FAILED,
    RTMP_ERROR_READ_FAILED,
    RTMP_ERROR_OUT_OF_MEMORY,
    RTMP_ERROR_INVALID_PARAM
} RTMPError;

const char *rtmp_error_string(RTMPError error);

#endif /* RTMP_UTILS_H */