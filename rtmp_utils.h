#ifndef RTMP_UTILS_H
#define RTMP_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <arpa/inet.h>

// Endian conversion macros
#define RTMP_HTONL(x) htonl(x)
#define RTMP_NTOHL(x) ntohl(x)
#define RTMP_HTONS(x) htons(x)
#define RTMP_NTOHS(x) ntohs(x)

// For 64-bit integers
#define RTMP_HTONLL(x) ((((uint64_t)htonl(x)) << 32) + htonl((x) >> 32))
#define RTMP_NTOHLL(x) ((((uint64_t)ntohl(x)) << 32) + ntohl((x) >> 32))

// Helper macros for 3-byte integers
#define RTMP_HTON24(x) ((((x) & 0xFF0000) >> 16) | ((x) & 0x00FF00) | (((x) & 0x0000FF) << 16))
#define RTMP_NTOH24(x) RTMP_HTON24(x)

// Buffer utilities
void rtmp_buffer_init(uint8_t *buffer, size_t size);
int rtmp_buffer_append(uint8_t *buffer, size_t *offset, size_t max_size, const uint8_t *data, size_t data_size);
int rtmp_buffer_read(const uint8_t *buffer, size_t *offset, size_t max_size, uint8_t *data, size_t data_size);

// Number utilities
uint32_t rtmp_get_three_bytes(const uint8_t *data);
void rtmp_set_three_bytes(uint8_t *data, uint32_t value);

// Time utilities
uint64_t rtmp_get_current_time(void);
uint32_t rtmp_get_uptime(void);

// String utilities
char* rtmp_strdup(const char *str);
void rtmp_string_to_lower(char *str);
int rtmp_string_ends_with(const char *str, const char *suffix);

// Debug utilities
void rtmp_hex_dump(const char *prefix, const uint8_t *data, size_t size);
const char* rtmp_get_message_type_string(uint8_t msg_type_id);

#endif // RTMP_UTILS_H