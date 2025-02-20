#include "rtmp_util.h"
#include "rtmp_log.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

// Funções para leitura de números
uint16_t read_uint16(const uint8_t* data) {
    return (data[0] << 8) | data[1];
}

uint32_t read_uint24(const uint8_t* data) {
    return (data[0] << 16) | (data[1] << 8) | data[2];
}

uint32_t read_uint32(const uint8_t* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

uint64_t read_uint64(const uint8_t* data) {
    return ((uint64_t)data[0] << 56) |
           ((uint64_t)data[1] << 48) |
           ((uint64_t)data[2] << 40) |
           ((uint64_t)data[3] << 32) |
           ((uint64_t)data[4] << 24) |
           ((uint64_t)data[5] << 16) |
           ((uint64_t)data[6] << 8) |
           data[7];
}

// Funções para escrita de números
void write_uint16(uint8_t* buffer, uint16_t value) {
    buffer[0] = (value >> 8) & 0xFF;
    buffer[1] = value & 0xFF;
}

void write_uint24(uint8_t* buffer, uint32_t value) {
    buffer[0] = (value >> 16) & 0xFF;
    buffer[1] = (value >> 8) & 0xFF;
    buffer[2] = value & 0xFF;
}

void write_uint32(uint8_t* buffer, uint32_t value) {
    buffer[0] = (value >> 24) & 0xFF;
    buffer[1] = (value >> 16) & 0xFF;
    buffer[2] = (value >> 8) & 0xFF;
    buffer[3] = value & 0xFF;
}

void write_uint64(uint8_t* buffer, uint64_t value) {
    buffer[0] = (value >> 56) & 0xFF;
    buffer[1] = (value >> 48) & 0xFF;
    buffer[2] = (value >> 40) & 0xFF;
    buffer[3] = (value >> 32) & 0xFF;
    buffer[4] = (value >> 24) & 0xFF;
    buffer[5] = (value >> 16) & 0xFF;
    buffer[6] = (value >> 8) & 0xFF;
    buffer[7] = value & 0xFF;
}

// Funções para manipulação de buffer
void* buffer_alloc(size_t size) {
    void* ptr = malloc(size);
    if (!ptr) {
        LOG_ERROR("Failed to allocate %zu bytes", size);
    }
    return ptr;
}

void* buffer_realloc(void* ptr, size_t size) {
    void* new_ptr = realloc(ptr, size);
    if (!new_ptr) {
        LOG_ERROR("Failed to reallocate to %zu bytes", size);
    }
    return new_ptr;
}

void buffer_free(void* ptr) {
    free(ptr);
}

// Funções para manipulação de strings
char* string_copy(const char* str) {
    if (!str) return NULL;
    char* copy = strdup(str);
    if (!copy) {
        LOG_ERROR("Failed to copy string");
    }
    return copy;
}

int string_equals(const char* str1, const char* str2) {
    if (!str1 || !str2) return 0;
    return strcmp(str1, str2) == 0;
}

// Funções de timestamp
uint32_t get_timestamp(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)tv.tv_sec;
}

uint32_t get_timestamp_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

// Funções de endianness
uint16_t swap_uint16(uint16_t value) {
    return ((value & 0xFF00) >> 8) |
           ((value & 0x00FF) << 8);
}

uint32_t swap_uint32(uint32_t value) {
    return ((value & 0xFF000000) >> 24) |
           ((value & 0x00FF0000) >> 8) |
           ((value & 0x0000FF00) << 8) |
           ((value & 0x000000FF) << 24);
}

uint64_t swap_uint64(uint64_t value) {
    return ((value & 0xFF00000000000000ULL) >> 56) |
           ((value & 0x00FF000000000000ULL) >> 40) |
           ((value & 0x0000FF0000000000ULL) >> 24) |
           ((value & 0x000000FF00000000ULL) >> 8) |
           ((value & 0x00000000FF000000ULL) << 8) |
           ((value & 0x0000000000FF0000ULL) << 24) |
           ((value & 0x000000000000FF00ULL) << 40) |
           ((value & 0x00000000000000FFULL) << 56);
}