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
#define RTMP_CHUNK_TYPE_0 0
#define RTMP_CHUNK_TYPE_1 1
#define RTMP_CHUNK_TYPE_2 2
#define RTMP_CHUNK_TYPE_3 3

// Funções principais
RTMPChunkStream* rtmp_chunk_stream_create(int socket_fd);
void rtmp_chunk_stream_destroy(RTMPChunkStream* cs);

int rtmp_chunk_read(RTMPChunkStream* cs, RTMPMessage* message);
int rtmp_chunk_write(RTMPChunkStream* cs, RTMPMessage* message);

void rtmp_chunk_set_size(RTMPChunkStream* cs, uint32_t size);
void rtmp_chunk_update_window(RTMPChunkStream* cs, uint32_t size);

#endif