// rtmp_chunk.h
#ifndef RTMP_CHUNK_H
#define RTMP_CHUNK_H

#include "rtmp_core.h"
#include "rtmp_protocol.h"
#include <stdint.h>

// Tamanhos de chunk
#define RTMP_CHUNK_SIZE_MAX 65536
#define RTMP_CHUNK_SIZE_MIN 128

// Formatos de chunk
#define RTMP_CHUNK_TYPE_0 0  // Chunk com cabeçalho completo
#define RTMP_CHUNK_TYPE_1 1  // Chunk com timestamp delta
#define RTMP_CHUNK_TYPE_2 2  // Chunk com timestamp delta reduzido
#define RTMP_CHUNK_TYPE_3 3  // Chunk sem cabeçalho

typedef struct {
    uint8_t* data;
    size_t size;
    size_t capacity;
    uint32_t timestamp;
} RTMPChunkData;

typedef struct {
    RTMPChunkData chunks[64];  // Array de chunks por chunk stream ID
    uint32_t chunk_size;
    uint32_t window_size;
    uint32_t bytes_received;
    uint32_t last_ack;
} RTMPChunkStream;

// Funções principais
RTMPChunkStream* rtmp_chunk_stream_create(void);
void rtmp_chunk_stream_destroy(RTMPChunkStream* cs);

int rtmp_chunk_read(RTMPSession* session, RTMPMessage* message);
int rtmp_chunk_write(RTMPSession* session, RTMPMessage* message);

void rtmp_chunk_set_size(RTMPChunkStream* cs, uint32_t size);
void rtmp_chunk_update_window(RTMPChunkStream* cs, uint32_t size);

#endif