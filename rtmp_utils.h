#ifndef RTMP_UTILS_H
#define RTMP_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <openssl/sha.h>
#include <zlib.h>

// Buffer structure
typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} rtmp_buffer_t;

// Time functions
uint64_t rtmp_utils_get_time_ms(void);
void rtmp_utils_sleep_ms(uint32_t milliseconds);

// Socket functions
int rtmp_utils_create_server_socket(int port);
int rtmp_utils_accept_connection(int server_socket);
int rtmp_utils_send(int socket, const void *data, size_t size, int timeout_ms);
int rtmp_utils_receive(int socket, void *buffer, size_t size, int timeout_ms);
int rtmp_utils_set_socket_nonblocking(int socket);
int rtmp_utils_set_socket_nodelay(int socket);
int rtmp_utils_set_socket_keepalive(int socket);

// Crypto functions
void rtmp_utils_init_random(void);
void rtmp_utils_random_bytes(uint8_t *buffer, size_t size);
void rtmp_utils_hmac_sha256(const uint8_t *key, size_t key_size,
                          const uint8_t *data, size_t data_size,
                          uint8_t *output);

// Compression functions
int rtmp_utils_compress(const uint8_t *input, size_t input_size,
                      uint8_t *output, size_t *output_size);
int rtmp_utils_decompress(const uint8_t *input, size_t input_size,
                        uint8_t *output, size_t *output_size);

// Byte order conversion
uint16_t rtmp_utils_swap16(uint16_t value);
uint32_t rtmp_utils_swap32(uint32_t value);
uint64_t rtmp_utils_swap64(uint64_t value);

// URL encoding/decoding
char* rtmp_utils_url_encode(const char *str);
char* rtmp_utils_url_decode(const char *str);

// Base64 encoding/decoding
char* rtmp_utils_base64_encode(const uint8_t *data, size_t size);
uint8_t* rtmp_utils_base64_decode(const char *str, size_t *size);

// Memory management
void* rtmp_utils_malloc(size_t size);
void* rtmp_utils_realloc(void *ptr, size_t new_size);
void rtmp_utils_free(void *ptr);

// Buffer management
rtmp_buffer_t* rtmp_utils_buffer_create(size_t initial_size);
int rtmp_utils_buffer_append(rtmp_buffer_t *buffer, const void *data, size_t size);
void rtmp_utils_buffer_clear(rtmp_buffer_t *buffer);
void rtmp_utils_buffer_destroy(rtmp_buffer_t *buffer);

// String utilities
char* rtmp_utils_strdup(const char *str);
int rtmp_utils_strcasecmp(const char *s1, const char *s2);

// Cleanup
void rtmp_utils_cleanup(void);

#endif // RTMP_UTILS_H