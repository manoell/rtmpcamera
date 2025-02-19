#include "rtmp_protocol.h"
#include "rtmp_log.h"
#include "rtmp_amf.h"
#include <string.h>
#include <stdarg.h>

static void write_uint16(uint8_t* buffer, uint16_t value) {
    buffer[0] = (value >> 8) & 0xFF;
    buffer[1] = value & 0xFF;
}

static void write_uint32(uint8_t* buffer, uint32_t value) {
    buffer[0] = (value >> 24) & 0xFF;
    buffer[1] = (value >> 16) & 0xFF;
    buffer[2] = (value >> 8) & 0xFF;
    buffer[3] = value & 0xFF;
}

static uint16_t read_uint16(const uint8_t* data) {
    return (data[0] << 8) | data[1];
}

static uint32_t read_uint32(const uint8_t* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

int rtmp_create_set_chunk_size(uint8_t* buffer, size_t len, uint32_t chunk_size) {
    if (!buffer || len < 4) return -1;
    
    write_uint32(buffer, chunk_size);
    return 4;
}

int rtmp_create_window_ack_size(uint8_t* buffer, size_t len, uint32_t window_size) {
    if (!buffer || len < 4) return -1;
    
    write_uint32(buffer, window_size);
    return 4;
}

int rtmp_create_set_peer_bandwidth(uint8_t* buffer, size_t len, uint32_t window_size, uint8_t limit_type) {
    if (!buffer || len < 5) return -1;
    
    write_uint32(buffer, window_size);
    buffer[4] = limit_type;
    return 5;
}

int rtmp_create_user_control(uint8_t* buffer, size_t len, uint16_t event_type, uint32_t event_data) {
    if (!buffer || len < 6) return -1;
    
    write_uint16(buffer, event_type);
    write_uint32(buffer + 2, event_data);
    return 6;
}

int rtmp_process_chunk_size(const uint8_t* data, size_t len, uint32_t* chunk_size) {
    if (!data || !chunk_size || len < 4) return -1;
    
    *chunk_size = read_uint32(data);
    if (*chunk_size < 1 || *chunk_size > 16777215) return -1;
    
    return 0;
}

int rtmp_process_window_ack_size(const uint8_t* data, size_t len, uint32_t* window_size) {
    if (!data || !window_size || len < 4) return -1;
    
    *window_size = read_uint32(data);
    return 0;
}

int rtmp_process_set_peer_bandwidth(const uint8_t* data, size_t len, uint32_t* window_size, uint8_t* limit_type) {
    if (!data || !window_size || !limit_type || len < 5) return -1;
    
    *window_size = read_uint32(data);
    *limit_type = data[4];
    return 0;
}

int rtmp_process_user_control(const uint8_t* data, size_t len, uint16_t* event_type, uint32_t* event_data) {
    if (!data || !event_type || !event_data || len < 6) return -1;
    
    *event_type = read_uint16(data);
    *event_data = read_uint32(data + 2);
    return 0;
}

int rtmp_create_command(uint8_t* buffer, size_t len, const char* command_name, double transaction_id, ...) {
    if (!buffer || !command_name || len < 3) return -1;
    
    size_t offset = 0;
    size_t bytes_written;
    
    // Codificar nome do comando
    if (amf_encode_string(buffer + offset, len - offset, command_name, &bytes_written) < 0) {
        return -1;
    }
    offset += bytes_written;
    
    // Codificar transaction ID
    if (amf_encode_number(buffer + offset, len - offset, transaction_id, &bytes_written) < 0) {
        return -1;
    }
    offset += bytes_written;
    
    // Codificar command object (null por padrão)
    buffer[offset++] = AMF0_NULL;
    
    va_list args;
    va_start(args, transaction_id);
    
    while (1) {
        const char* type = va_arg(args, const char*);
        if (!type) break;
        
        if (strcmp(type, "string") == 0) {
            const char* str = va_arg(args, const char*);
            if (amf_encode_string(buffer + offset, len - offset, str, &bytes_written) < 0) {
                va_end(args);
                return -1;
            }
            offset += bytes_written;
        }
        else if (strcmp(type, "number") == 0) {
            double num = va_arg(args, double);
            if (amf_encode_number(buffer + offset, len - offset, num, &bytes_written) < 0) {
                va_end(args);
                return -1;
            }
            offset += bytes_written;
        }
        else if (strcmp(type, "boolean") == 0) {
            int boolean = va_arg(args, int);
            if (amf_encode_boolean(buffer + offset, len - offset, boolean, &bytes_written) < 0) {
                va_end(args);
                return -1;
            }
            offset += bytes_written;
        }
        else if (strcmp(type, "null") == 0) {
            buffer[offset++] = AMF0_NULL;
        }
    }
    
    va_end(args);
    return offset;
}