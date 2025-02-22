#ifndef RTMP_CHUNK_H
#define RTMP_CHUNK_H

#include <stdint.h>
#include <stdlib.h>

// RTMP packet structure
typedef struct RTMPPacket {
    uint8_t m_headerType;
    uint32_t m_nChannel;
    uint32_t m_nTimeStamp;
    uint32_t m_nBodySize;
    uint8_t m_packetType;
    uint32_t m_nInfoField2;
    uint8_t *m_body;
} RTMPPacket;

// Initialize chunk handling
void rtmp_chunk_init(void);

// Set chunk size
void rtmp_chunk_set_size(uint32_t size);

// Get total size needed for packet serialization
uint32_t rtmp_chunk_get_size(const RTMPPacket *packet);

// Serialize packet into chunks
size_t rtmp_chunk_serialize(const RTMPPacket *packet, uint8_t *buffer, size_t buffer_size);

// Parse chunks into packet
RTMPPacket* rtmp_chunk_parse(const uint8_t *buffer, size_t size);

// Free packet memory
void rtmp_packet_free(RTMPPacket *packet);

// Reset chunk stream state
void rtmp_chunk_reset_stream(uint32_t chunk_stream_id);

// Reset all chunk streams
void rtmp_chunk_reset_all(void);

#endif // RTMP_CHUNK_H