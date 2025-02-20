#ifndef RTMP_CHUNK_H
#define RTMP_CHUNK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RTMP_MAX_CHUNK_STREAMS 64
#define RTMP_DEFAULT_CHUNK_SIZE 128

typedef struct {
    uint8_t fmt;               // Formato do chunk (0-3)
    uint32_t csid;            // Chunk Stream ID
    uint32_t timestamp;       // Timestamp absoluto
    uint32_t timestamp_delta; // Delta do timestamp
    uint32_t length;          // Comprimento da mensagem
    uint8_t type;            // Tipo da mensagem
    uint32_t stream_id;      // ID do stream
    uint8_t* data;           // Dados do chunk
    size_t data_size;        // Tamanho atual dos dados
    size_t bytes_read;       // Bytes já lidos
    int extended_timestamp;   // Flag para timestamp estendido
} RTMPChunk;

typedef struct {
    RTMPChunk* chunks[RTMP_MAX_CHUNK_STREAMS];  // Array de chunks ativos
    uint32_t chunk_size;                        // Tamanho do chunk atual
    uint32_t in_chunk_size;                     // Tamanho do chunk de entrada
    uint32_t out_chunk_size;                    // Tamanho do chunk de saída
    uint32_t ack_window;                        // Janela de acknowledgement
    uint32_t bytes_in;                          // Bytes recebidos
    uint32_t bytes_out;                         // Bytes enviados
    uint32_t last_ack;                          // Último ack enviado
} RTMPChunkStream;

// Funções de inicialização/destruição
RTMPChunkStream* rtmp_chunk_stream_create(void);
void rtmp_chunk_stream_destroy(RTMPChunkStream* cs);

// Funções de processamento de chunks
int rtmp_chunk_read(RTMPChunkStream* cs, const uint8_t* data, size_t len, size_t* bytes_read);
int rtmp_chunk_write(RTMPChunkStream* cs, RTMPChunk* chunk, uint8_t* buffer, size_t len, size_t* bytes_written);

// Funções de gerenciamento de chunks
RTMPChunk* rtmp_chunk_create(void);
void rtmp_chunk_destroy(RTMPChunk* chunk);
void rtmp_chunk_reset(RTMPChunk* chunk);

// Funções de utilidade
int rtmp_chunk_update_size(RTMPChunkStream* cs, uint32_t size);
int rtmp_chunk_acknowledge(RTMPChunkStream* cs, uint32_t size);
void rtmp_chunk_reset_stream(RTMPChunkStream* cs, uint32_t csid);

#ifdef __cplusplus
}
#endif

#endif // RTMP_CHUNK_H