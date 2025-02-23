#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include "rtmp_handshake.h"
#include "rtmp_utils.h"

#define RTMP_HANDSHAKE_KEYSIZE 32

// Chaves de criptografia para diferentes tipos de handshake
static const uint8_t rtmp_handshake_key_fp[] = {
    'G', 'e', 'n', 'u', 'i', 'n', 'e', ' ', 'A', 'd', 'o', 'b', 'e', ' ',
    'F', 'l', 'a', 's', 'h', ' ', 'P', 'l', 'a', 'y', 'e', 'r', ' ',
    '0', '0', '1', 0xF0, 0xEE
};

static const uint8_t rtmp_handshake_key_fms[] = {
    'G', 'e', 'n', 'u', 'i', 'n', 'e', ' ', 'A', 'd', 'o', 'b', 'e', ' ',
    'F', 'l', 'a', 's', 'h', ' ', 'M', 'e', 'd', 'i', 'a', ' ',
    'S', 'e', 'r', 'v', 'e', 'r'
};

struct rtmp_handshake_ctx {
    uint8_t version;
    uint8_t *c1;
    uint8_t *s1;
    uint8_t *c2;
    uint8_t *s2;
    size_t c1_size;
    size_t s1_size;
    size_t c2_size;
    size_t s2_size;
    int state;
    int digest_type;
    uint8_t digest[RTMP_HANDSHAKE_DIGEST_SIZE];
};

static void rtmp_handshake_init_packet(uint8_t *packet) {
    uint32_t timestamp = (uint32_t)time(NULL);
    memset(packet, 0, RTMP_HANDSHAKE_SIZE);
    memcpy(packet, &timestamp, 4);
    
    // Gerar bytes aleatórios para o resto do pacote
    for (int i = 8; i < RTMP_HANDSHAKE_SIZE; i++) {
        packet[i] = rand() & 0xFF;
    }
}

static int rtmp_handshake_calculate_digest(const uint8_t *data, size_t size,
                                         const uint8_t *key, size_t key_size,
                                         uint8_t *digest) {
    unsigned int digest_len;
    HMAC_CTX *ctx = HMAC_CTX_new();
    if (!ctx) return 0;
    
    HMAC_Init_ex(ctx, key, key_size, EVP_sha256(), NULL);
    HMAC_Update(ctx, data, size);
    HMAC_Final(ctx, digest, &digest_len);
    
    HMAC_CTX_free(ctx);
    return (digest_len == RTMP_HANDSHAKE_DIGEST_SIZE);
}

static int rtmp_handshake_find_digest(const uint8_t *data, size_t size,
                                    const uint8_t *key, size_t key_size) {
    uint8_t digest[RTMP_HANDSHAKE_DIGEST_SIZE];
    int offset = -1;
    
    // Procurar digest em posições válidas
    for (size_t i = 0; i < size - RTMP_HANDSHAKE_DIGEST_SIZE; i++) {
        if (rtmp_handshake_calculate_digest(data, i, key, key_size, digest) &&
            memcmp(digest, data + i, RTMP_HANDSHAKE_DIGEST_SIZE) == 0) {
            offset = i;
            break;
        }
    }
    
    return offset;
}

int rtmp_handshake_client(rtmp_connection_t *conn) {
    rtmp_handshake_ctx_t *ctx;
    uint8_t *c1;
    int ret;
    
    ctx = (rtmp_handshake_ctx_t*)calloc(1, sizeof(rtmp_handshake_ctx_t));
    if (!ctx) return 0;
    
    // Enviar versão
    ctx->version = RTMP_HANDSHAKE_VERSION;
    ret = rtmp_send(conn, &ctx->version, 1);
    if (ret != 1) {
        free(ctx);
        return 0;
    }
    
    // Preparar e enviar C1
    c1 = (uint8_t*)malloc(RTMP_HANDSHAKE_SIZE);
    if (!c1) {
        free(ctx);
        return 0;
    }
    
    rtmp_handshake_init_packet(c1);
    ctx->c1 = c1;
    ctx->c1_size = RTMP_HANDSHAKE_SIZE;
    
    // Calcular e inserir digest
    uint8_t digest[RTMP_HANDSHAKE_DIGEST_SIZE];
    rtmp_handshake_calculate_digest(c1, RTMP_HANDSHAKE_SIZE - RTMP_HANDSHAKE_DIGEST_SIZE,
                                  rtmp_handshake_key_fp, sizeof(rtmp_handshake_key_fp),
                                  digest);
    memcpy(c1 + RTMP_HANDSHAKE_SIZE - RTMP_HANDSHAKE_DIGEST_SIZE,
           digest, RTMP_HANDSHAKE_DIGEST_SIZE);
    
    ret = rtmp_send(conn, c1, RTMP_HANDSHAKE_SIZE);
    if (ret != RTMP_HANDSHAKE_SIZE) {
        free(c1);
        free(ctx);
        return 0;
    }
    
    conn->handshake = ctx;
    return 1;
}

int rtmp_handshake_server(rtmp_connection_t *conn) {
    rtmp_handshake_ctx_t *ctx;
    uint8_t *s1;
    int ret;
    
    ctx = (rtmp_handshake_ctx_t*)calloc(1, sizeof(rtmp_handshake_ctx_t));
    if (!ctx) return 0;
    
    // Receber versão
    ret = rtmp_recv(conn, &ctx->version, 1);
    if (ret != 1 || ctx->version != RTMP_HANDSHAKE_VERSION) {
        free(ctx);
        return 0;
    }
    
    // Preparar e enviar S1
    s1 = (uint8_t*)malloc(RTMP_HANDSHAKE_SIZE);
    if (!s1) {
        free(ctx);
        return 0;
    }
    
    rtmp_handshake_init_packet(s1);
    ctx->s1 = s1;
    ctx->s1_size = RTMP_HANDSHAKE_SIZE;
    
    // Calcular e inserir digest
    uint8_t digest[RTMP_HANDSHAKE_DIGEST_SIZE];
    rtmp_handshake_calculate_digest(s1, RTMP_HANDSHAKE_SIZE - RTMP_HANDSHAKE_DIGEST_SIZE,
                                  rtmp_handshake_key_fms, sizeof(rtmp_handshake_key_fms),
                                  digest);
    memcpy(s1 + RTMP_HANDSHAKE_SIZE - RTMP_HANDSHAKE_DIGEST_SIZE,
           digest, RTMP_HANDSHAKE_DIGEST_SIZE);
    
    ret = rtmp_send(conn, s1, RTMP_HANDSHAKE_SIZE);
    if (ret != RTMP_HANDSHAKE_SIZE) {
        free(s1);
        free(ctx);
        return 0;
    }
    
    conn->handshake = ctx;
    return 1;
}

int rtmp_handshake_process(rtmp_connection_t *conn, const uint8_t *data, size_t size) {
    rtmp_handshake_ctx_t *ctx = conn->handshake;
    if (!ctx) return 0;
    
    switch (ctx->state) {
        case 0: // Aguardando S1
            if (size < RTMP_HANDSHAKE_SIZE) return 0;
            
            ctx->s1 = (uint8_t*)malloc(RTMP_HANDSHAKE_SIZE);
            if (!ctx->s1) return 0;
            
            memcpy(ctx->s1, data, RTMP_HANDSHAKE_SIZE);
            ctx->s1_size = RTMP_HANDSHAKE_SIZE;
            
            // Verificar digest do servidor
            if (rtmp_handshake_find_digest(ctx->s1, RTMP_HANDSHAKE_SIZE,
                                         rtmp_handshake_key_fms,
                                         sizeof(rtmp_handshake_key_fms)) < 0) {
                return 0;
            }
            
            // Preparar e enviar C2
            ctx->c2 = (uint8_t*)malloc(RTMP_HANDSHAKE_SIZE);
            if (!ctx->c2) return 0;
            
            memcpy(ctx->c2, ctx->s1, RTMP_HANDSHAKE_SIZE);
            ctx->c2_size = RTMP_HANDSHAKE_SIZE;
            
            if (rtmp_send(conn, ctx->c2, RTMP_HANDSHAKE_SIZE) != RTMP_HANDSHAKE_SIZE) {
                return 0;
            }
            
            ctx->state = 1;
            break;
            
        case 1: // Aguardando S2
            if (size < RTMP_HANDSHAKE_SIZE) return 0;
            
            ctx->s2 = (uint8_t*)malloc(RTMP_HANDSHAKE_SIZE);
            if (!ctx->s2) return 0;
            
            memcpy(ctx->s2, data, RTMP_HANDSHAKE_SIZE);
            ctx->s2_size = RTMP_HANDSHAKE_SIZE;
            
            // Verificar se S2 corresponde a C1
            if (memcmp(ctx->s2, ctx->c1, RTMP_HANDSHAKE_SIZE) != 0) {
                return 0;
            }
            
            ctx->state = 2;
            break;
            
        default:
            return 0;
    }
    
    return 1;
}

int rtmp_handshake_is_done(rtmp_connection_t *conn) {
    rtmp_handshake_ctx_t *ctx = conn->handshake;
    return (ctx && ctx->state == 2);
}

int rtmp_handshake_generate_signature(const uint8_t *data, size_t size,
                                    const uint8_t *key, size_t key_size,
                                    uint8_t *signature) {
    return rtmp_handshake_calculate_digest(data, size, key, key_size, signature);
}

int rtmp_handshake_verify_signature(const uint8_t *data, size_t size,
                                  const uint8_t *key, size_t key_size,
                                  const uint8_t *signature) {
    uint8_t calculated[RTMP_HANDSHAKE_DIGEST_SIZE];
    
    if (!rtmp_handshake_calculate_digest(data, size, key, key_size, calculated)) {
        return 0;
    }
    
    return (memcmp(calculated, signature, RTMP_HANDSHAKE_DIGEST_SIZE) == 0);
}