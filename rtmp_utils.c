#include "rtmp_utils.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/time.h>
#include <unistd.h>
#include <pthread.h>

static RTMPLogLevel current_log_level = RTMP_LOG_INFO;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// Buffer management implementations
uint8_t *rtmp_buffer_create(size_t size) {
    uint8_t *buffer = (uint8_t *)malloc(size);
    if (buffer) {
        memset(buffer, 0, size);
    }
    return buffer;
}

void rtmp_buffer_free(uint8_t *buffer) {
    if (buffer) {
        free(buffer);
    }
}

void rtmp_buffer_reset(uint8_t *buffer) {
    if (buffer) {
        memset(buffer, 0, rtmp_buffer_size(buffer));
    }
}

size_t rtmp_buffer_size(uint8_t *buffer) {
    if (!buffer) return 0;
    return sizeof(buffer);
}

// Network utility implementations
bool rtmp_is_network_reachable(void) {
    // Basic implementation - can be enhanced with actual network checks
    return true;
}

uint32_t rtmp_get_timestamp(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void rtmp_sleep_ms(uint32_t ms) {
    usleep(ms * 1000);
}

// RTMP packet implementations
RTMPPacket *rtmp_packet_create(void) {
    RTMPPacket *packet = (RTMPPacket *)malloc(sizeof(RTMPPacket));
    if (packet) {
        memset(packet, 0, sizeof(RTMPPacket));
    }
    return packet;
}

void rtmp_packet_free(RTMPPacket *packet) {
    if (packet) {
        if (packet->data) {
            free(packet->data);
        }
        free(packet);
    }
}

RTMPPacket *rtmp_packet_copy(const RTMPPacket *packet) {
    if (!packet) return NULL;
    
    RTMPPacket *copy = rtmp_packet_create();
    if (!copy) return NULL;
    
    copy->type = packet->type;
    copy->timestamp = packet->timestamp;
    copy->streamId = packet->streamId;
    
    if (packet->data && packet->size > 0) {
        if (!rtmp_packet_alloc(copy, packet->size)) {
            rtmp_packet_free(copy);
            return NULL;
        }
        memcpy(copy->data, packet->data, packet->size);
    }
    
    return copy;
}

bool rtmp_packet_alloc(RTMPPacket *packet, size_t size) {
    if (!packet) return false;
    
    if (packet->data) {
        free(packet->data);
    }
    
    packet->data = (uint8_t *)malloc(size);
    if (!packet->data) {
        packet->size = 0;
        return false;
    }
    
    packet->size = size;
    return true;
}

// Logging implementations
void rtmp_log(RTMPLogLevel level, const char *format, ...) {
    if (level > current_log_level) return;
    
    pthread_mutex_lock(&log_mutex);
    
    va_list args;
    va_start(args, format);
    
    const char *level_str;
    switch (level) {
        case RTMP_LOG_ERROR:   level_str = "ERROR"; break;
        case RTMP_LOG_WARNING: level_str = "WARN"; break;
        case RTMP_LOG_INFO:    level_str = "INFO"; break;
        case RTMP_LOG_DEBUG:   level_str = "DEBUG"; break;
        default:               level_str = "UNKNOWN"; break;
    }
    
    printf("[RTMP-%s] ", level_str);
    vprintf(format, args);
    printf("\n");
    
    va_end(args);
    pthread_mutex_unlock(&log_mutex);
}

void rtmp_set_log_level(RTMPLogLevel level) {
    current_log_level = level;
}

RTMPLogLevel rtmp_get_log_level(void) {
    return current_log_level;
}

// AMF encoding/decoding implementations
int amf_encode_string(char *output, const char *str) {
    if (!output || !str) return 0;
    
    size_t len = strlen(str);
    output[0] = AMF_STRING;
    output[1] = (len >> 8) & 0xFF;
    output[2] = len & 0xFF;
    memcpy(&output[3], str, len);
    
    return len + 3;
}

int amf_encode_number(char *output, double number) {
    if (!output) return 0;
    
    output[0] = AMF_NUMBER;
    uint8_t *double_ptr = (uint8_t *)&number;
    for (int i = 0; i < 8; i++) {
        output[i + 1] = double_ptr[7 - i];
    }
    
    return 9;
}

int amf_encode_boolean(char *output, bool boolean) {
    if (!output) return 0;
    
    output[0] = AMF_BOOLEAN;
    output[1] = boolean ? 1 : 0;
    
    return 2;
}

size_t amf_decode_string(const char *input, char **str) {
    if (!input || !str) return 0;
    
    if (input[0] != AMF_STRING) return 0;
    
    uint16_t len = (input[1] << 8) | input[2];
    *str = (char *)malloc(len + 1);
    if (!*str) return 0;
    
    memcpy(*str, &input[3], len);
    (*str)[len] = '\0';
    
    return len + 3;
}

size_t amf_decode_number(const char *input, double *number) {
    if (!input || !number) return 0;
    
    if (input[0] != AMF_NUMBER) return 0;
    
    uint8_t *double_ptr = (uint8_t *)number;
    for (int i = 0; i < 8; i++) {
        double_ptr[7 - i] = input[i + 1];
    }
    
    return 9;
}

size_t amf_decode_boolean(const char *input, bool *boolean) {
    if (!input || !boolean) return 0;
    
    if (input[0] != AMF_BOOLEAN) return 0;
    
    *boolean = input[1] != 0;
    
    return 2;
}

// Performance monitoring implementations
#define MAX_PERF_ENTRIES 100

typedef struct {
    char name[64];
    struct timeval start;
    struct timeval end;
    uint64_t duration;
} PerfEntry;

static PerfEntry perf_entries[MAX_PERF_ENTRIES];
static int perf_count = 0;
static pthread_mutex_t perf_mutex = PTHREAD_MUTEX_INITIALIZER;

void rtmp_perf_start(const char *name) {
    pthread_mutex_lock(&perf_mutex);
    
    if (perf_count < MAX_PERF_ENTRIES) {
        strncpy(perf_entries[perf_count].name, name, 63);
        gettimeofday(&perf_entries[perf_count].start, NULL);
        perf_count++;
    }
    
    pthread_mutex_unlock(&perf_mutex);
}

void rtmp_perf_end(const char *name) {
    pthread_mutex_lock(&perf_mutex);
    
    for (int i = 0; i < perf_count; i++) {
        if (strcmp(perf_entries[i].name, name) == 0) {
            gettimeofday(&perf_entries[i].end, NULL);
            perf_entries[i].duration = 
                (perf_entries[i].end.tv_sec - perf_entries[i].start.tv_sec) * 1000000 +
                (perf_entries[i].end.tv_usec - perf_entries[i].start.tv_usec);
            break;
        }
    }
    
    pthread_mutex_unlock(&perf_mutex);
}

void rtmp_perf_reset(void) {
    pthread_mutex_lock(&perf_mutex);
    perf_count = 0;
    pthread_mutex_unlock(&perf_mutex);
}

void rtmp_perf_print_stats(void) {
    pthread_mutex_lock(&perf_mutex);
    
    printf("\n=== Performance Statistics ===\n");
    for (int i = 0; i < perf_count; i++) {
        printf("%s: %llu microseconds\n", 
               perf_entries[i].name, 
               perf_entries[i].duration);
    }
    printf("===========================\n");
    
    pthread_mutex_unlock(&perf_mutex);
}

// Error handling implementations
static const char *error_strings[] = {
    "No error",
    "Failed to create socket",
    "Failed to connect socket",
    "Handshake failed",
    "Publish failed",
    "Write failed",
    "Read failed",
    "Out of memory",
    "Invalid parameter"
};

const char *rtmp_error_string(RTMPError error) {
    if (error < 0 || error >= sizeof(error_strings)/sizeof(error_strings[0])) {
        return "Unknown error";
    }
    return error_strings[error];
}