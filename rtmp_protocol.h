#ifndef RTMP_PROTOCOL_H
#define RTMP_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "rtmp_chunk.h"

// RTMP default port
#define RTMP_DEFAULT_PORT 1935

// Initialize protocol handling
void rtmp_protocol_init(void);

// Create various RTMP protocol packets
RTMPPacket* rtmp_protocol_create_connect(const char *app, const char *tcUrl);
RTMPPacket* rtmp_protocol_create_play(const char *stream_name);
RTMPPacket* rtmp_protocol_create_publish(const char *stream_name);
RTMPPacket* rtmp_protocol_create_set_chunk_size(uint32_t chunk_size);
RTMPPacket* rtmp_protocol_create_window_ack_size(uint32_t window_size);
RTMPPacket* rtmp_protocol_create_ping(void);

// Parse RTMP URL into components
bool rtmp_protocol_parse_url(const char *url, char *hostname, size_t hostname_size,
                           int *port, char *app_name, size_t app_size,
                           char *stream_name, size_t stream_size);

// Extract values from received packets
uint32_t rtmp_protocol_get_chunk_size(const RTMPPacket *packet);
uint32_t rtmp_protocol_get_window_size(const RTMPPacket *packet);

// Handle received packets
void rtmp_protocol_handle_packet(const RTMPPacket *packet);

// Stream management
void rtmp_protocol_set_stream_id(uint32_t stream_id);

#endif // RTMP_PROTOCOL_H