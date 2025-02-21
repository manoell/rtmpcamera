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

// Estrutura do header do chunk
typedef struct {
    uint8_t fmt;           // Formato do chunk (0-3)
    uint32_t csid;         // Chunk Stream ID
    uint32_t timestamp;    // Timestamp absoluto ou delta
    uint32_t length;       // Tamanho da mensagem
    uint8_t type_id;       // Tipo da mensagem
    uint32_t stream_id;    // ID do stream
} RTMPChunkHeader;

// Funções
int rtmp_read_chunk_header(RTMPClient *client, RTMPChunkHeader *header);

#endif