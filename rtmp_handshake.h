#ifndef RTMP_HANDSHAKE_H
#define RTMP_HANDSHAKE_H

#include <stdint.h>
#include "rtmp_core.h"

// Tamanhos do handshake
#define RTMP_HANDSHAKE_VERSION       0x03
#define RTMP_HANDSHAKE_SIZE         1536
#define RTMP_HANDSHAKE_DIGEST_SIZE   32

// Flags de digest
#define RTMP_HANDSHAKE_DIGEST_NONE   0x00
#define RTMP_HANDSHAKE_DIGEST_FP     0x01
#define RTMP_HANDSHAKE_DIGEST_FMS    0x02

// Estrutura de contexto do handshake
typedef struct rtmp_handshake_ctx rtmp_handshake_ctx_t;

// Inicia handshake como cliente
int rtmp_handshake_client(rtmp_connection_t *conn);

// Inicia handshake como servidor
int rtmp_handshake_server(rtmp_connection_t *conn);

// Verifica se handshake está completo
int rtmp_handshake_is_done(rtmp_connection_t *conn);

// Processa dados recebidos durante handshake
int rtmp_handshake_process(rtmp_connection_t *conn, const uint8_t *data, size_t size);

// Gera signature para validação
int rtmp_handshake_generate_signature(const uint8_t *data, size_t size, 
                                    const uint8_t *key, size_t key_size,
                                    uint8_t *signature);

// Verifica signature
int rtmp_handshake_verify_signature(const uint8_t *data, size_t size,
                                  const uint8_t *key, size_t key_size,
                                  const uint8_t *signature);

#endif // RTMP_HANDSHAKE_H