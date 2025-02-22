#include "rtmp_utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <sys/time.h>

// Buffer utilities
void rtmp_buffer_init(uint8_t *buffer, size_t size) {
    if (buffer && size > 0) {
        memset(buffer, 0, size);
    }
}

int rtmp_buffer_append(uint8_t *buffer, size_t *offset, size_t max_size, 
                      const uint8_t *data, size_t data_size) {
    if (!buffer || !offset || !data || !data_size) return -1;
    
    if (*offset + data_size > max_size) {
        return -1;
    }
    
    memcpy(buffer + *offset, data, data_size);
    *offset += data_size;
    
    return 0;
}

int rtmp_buffer_read(const uint8_t *buffer, size_t *offset, size_t max_size,
                    uint8_t *data, size_t data_size) {
    if (!buffer || !offset || !data || !data_size) return -1;
    
    if (*offset + data_size > max_size) {
        return -1;
    }
    
    memcpy(data, buffer + *offset, data_size);
    *offset += data_size;
    
    return 0;
}

// Number utilities
uint32_t rtmp_get_three_bytes(const uint8_t *data) {
    if (!data) return 0;
    
    return (data[0] << 16) | (data[1] << 8) | data[2];
}

void rtmp_set_three_bytes(uint8_t *data, uint32_t value) {
    if (!data) return;
    
    data[0] = (value >> 16) & 0xFF;
    data[1] = (value >> 8) & 0xFF;
    data[2] = value & 0xFF;
}

// Time utilities
uint64_t rtmp_get_current_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static uint64_t start_time = 0;

uint32_t rtmp_get_uptime(void) {
    if (start_time == 0) {
        start_time = rtmp_get_current_time();
        return 0;
    }
    
    uint64_t current_time = rtmp_get_current_time();
    return (uint32_t)(current_time - start_time);
}

// String utilities
char* rtmp_strdup(const char *str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    char *dup = (char*)malloc(len + 1);
    if (!dup) return NULL;
    
    memcpy(dup, str, len);
    dup[len] = '\0';
    
    return dup;
}

void rtmp_string_to_lower(char *str) {
    if (!str) return;
    
    for (char *p = str; *p; p++) {
        *p = tolower(*p);
    }
}

int rtmp_string_ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    
    if (suffix_len > str_len) return 0;
    
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

// Debug utilities
void rtmp_hex_dump(const char *prefix, const uint8_t *data, size_t size) {
    if (!prefix || !data || !size) return;
    
    printf("%s [%zu bytes]:\n", prefix, size);
    
    for (size_t i = 0; i < size; i++) {
        if (i % 16 == 0) {
            printf("%08zx  ", i);
        }
        
        printf("%02x ", data[i]);
        
        if ((i + 1) % 16 == 0) {
            printf("  ");
            for (size_t j = i - 15; j <= i; j++) {
                if (isprint(data[j])) {
                    printf("%c", data[j]);
                } else {
                    printf(".");
                }
            }
            printf("\n");
        }
    }
    
    // Print remaining bytes if any
    if (size % 16 != 0) {
        size_t remaining = size % 16;
        size_t padding = 16 - remaining;
        
        // Print padding spaces
        for (size_t i = 0; i < padding; i++) {
            printf("   ");
        }
        
        printf("  ");
        
        // Print ASCII representation
        for (size_t i = size - remaining; i < size; i++) {
            if (isprint(data[i])) {
                printf("%c", data[i]);
            } else {
                printf(".");
            }
        }
        printf("\n");
    }
    
    printf("\n");
}

const char* rtmp_get_message_type_string(uint8_t msg_type_id) {
    switch (msg_type_id) {
        case 1:  return "Set Chunk Size";
        case 2:  return "Abort Message";
        case 3:  return "Acknowledgement";
        case 4:  return "User Control Message";
        case 5:  return "Window Acknowledgement Size";
        case 6:  return "Set Peer Bandwidth";
        case 8:  return "Audio Message";
        case 9:  return "Video Message";
        case 15: return "Data Message (AMF3)";
        case 16: return "Shared Object Message (AMF3)";
        case 17: return "Command Message (AMF3)";
        case 18: return "Data Message (AMF0)";
        case 19: return "Shared Object Message (AMF0)";
        case 20: return "Command Message (AMF0)";
        case 22: return "Aggregate Message";
        default: return "Unknown Message Type";
    }
}