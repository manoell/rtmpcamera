#ifndef RTMP_UTIL_H
#define RTMP_UTIL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Funções para leitura de números
uint16_t read_uint16(const uint8_t* data);
uint32_t read_uint24(const uint8_t* data);
uint32_t read_uint32(const uint8_t* data);
uint64_t read_uint64(const uint8_t* data);

// Funções para escrita de números
void write_uint16(uint8_t* buffer, uint16_t value);
void write_uint24(uint8_t* buffer, uint32_t value);
void write_uint32(uint8_t* buffer, uint32_t value);
void write_uint64(uint8_t* buffer, uint64_t value);

// Funções para manipulação de buffer
void* buffer_alloc(size_t size);
void* buffer_realloc(void* ptr, size_t size);
void buffer_free(void* ptr);

// Funções para manipulação de strings
char* string_copy(const char* str);
int string_equals(const char* str1, const char* str2);

// Funções de timestamp
uint32_t get_timestamp(void);
uint32_t get_timestamp_ms(void);

// Funções de endianness
uint16_t swap_uint16(uint16_t value);
uint32_t swap_uint32(uint32_t value);
uint64_t swap_uint64(uint64_t value);

#ifdef __cplusplus
}
#endif

#endif // RTMP_UTIL_H