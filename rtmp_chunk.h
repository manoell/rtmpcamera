#ifndef RTMP_CHUNK_H
#define RTMP_CHUNK_H

#include <stdint.h>
#include <stddef.h>

#define RTMP_CHUNK_SIZE_MAX 65536

typedef struct RTMPChunk {
    uint8_t fmt;
    uint32_t csid;
    uint32_t timestamp;
    uint32_t timestamp_delta;
    uint32_t length;
    uint32_t type;
    uint32_t stream_id;
    uint8_t* data;
    size_t data_size;
    size_t received;
} RTMPChunk;

RTMPChunk* rtmp_chunk_create(void);
void rtmp_chunk_destroy(RTMPChunk* chunk);
int rtmp_chunk_parse(RTMPChunk* chunk, const uint8_t* data, size_t len, size_t* bytes_read);
int rtmp_chunk_serialize(const RTMPChunk* chunk, uint8_t* buffer, size_t len, size_t* bytes_written);

#endif // RTMP_CHUNK_H