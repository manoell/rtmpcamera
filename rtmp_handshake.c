#include <string.h>
#include <stdlib.h>
#include <openssl/ssl.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include "rtmp_handshake.h"
#include "rtmp_core.h"

#define RTMP_HANDSHAKE_SIZE 1536
#define RTMP_VERSION 3
#define GENUINE_FP_KEY "Genuine Adobe Flash Player 001"
#define GENUINE_FMS_KEY "Genuine Adobe Flash Media Server 001"

typedef struct {
    uint8_t version;
    uint8_t time[4];
    uint8_t zero[4];
    uint8_t random[RTMP_HANDSHAKE_SIZE - 8];
} rtmp_handshake_t;

static const uint8_t rtmp_client_version[] = {
    0x0C, 0x00, 0x0D, 0x0E
};

static const uint8_t rtmp_server_version[] = {
    0x0D, 0x0E, 0x0A, 0x0D
};

static void generate_random_data(uint8_t *buffer, size_t size) {
    RAND_bytes(buffer, size);
}

static uint32_t get_current_timestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static void calculate_digest(const uint8_t *message, size_t message_len,
                           const uint8_t *key, size_t key_len,
                           uint8_t *digest) {
    unsigned int digest_len;
    HMAC_CTX *ctx = HMAC_CTX_new();
    HMAC_Init_ex(ctx, key, key_len, EVP_sha256(), NULL);
    HMAC_Update(ctx, message, message_len);
    HMAC_Final(ctx, digest, &digest_len);
    HMAC_CTX_free(ctx);
}

static void prepare_complex_handshake(uint8_t *handshake, 
                                    const uint8_t *key,
                                    bool is_server) {
    uint8_t digest[SHA256_DIGEST_LENGTH];
    const uint8_t *version = is_server ? rtmp_server_version : rtmp_client_version;
    
    // Inicializa o handshake com dados aleatórios
    generate_random_data(handshake, RTMP_HANDSHAKE_SIZE);
    
    // Define a versão
    memcpy(handshake, version, sizeof(rtmp_client_version));
    
    // Calcula e insere o digest
    calculate_digest(handshake, RTMP_HANDSHAKE_SIZE - SHA256_DIGEST_LENGTH,
                    key, strlen((char*)key), digest);
    memcpy(handshake + RTMP_HANDSHAKE_SIZE - SHA256_DIGEST_LENGTH,
           digest, SHA256_DIGEST_LENGTH);
}

static bool verify_complex_handshake(const uint8_t *handshake,
                                   const uint8_t *key) {
    uint8_t digest[SHA256_DIGEST_LENGTH];
    uint8_t comparison_digest[SHA256_DIGEST_LENGTH];
    
    // Extrai o digest original
    memcpy(comparison_digest,
           handshake + RTMP_HANDSHAKE_SIZE - SHA256_DIGEST_LENGTH,
           SHA256_DIGEST_LENGTH);
    
    // Calcula o digest para verificação
    calculate_digest(handshake, RTMP_HANDSHAKE_SIZE - SHA256_DIGEST_LENGTH,
                    key, strlen((char*)key), digest);
    
    return memcmp(digest, comparison_digest, SHA256_DIGEST_LENGTH) == 0;
}

// Sistema de buffer adaptativo
void rtmp_init_adaptive_buffer(rtmp_session_t *session) {
    session->buffer_size = RTMP_INITIAL_BUFFER_SIZE;
    session->buffer = malloc(session->buffer_size);
    session->adaptive_buffer_enabled = true;
    session->last_buffer_adjustment = get_current_timestamp();
    session->packet_stats.received = 0;
    session->packet_stats.lost = 0;
}

float calculate_packet_loss_rate(rtmp_session_t *session) {
    if (session->packet_stats.received == 0) {
        return 0.0f;
    }
    return (float)session->packet_stats.lost / 
           (float)(session->packet_stats.received + session->packet_stats.lost);
}

void rtmp_adjust_buffer(rtmp_session_t *session) {
    uint32_t current_time = get_current_timestamp();
    
    if (current_time - session->last_buffer_adjustment < BUFFER_ADJUSTMENT_INTERVAL) {
        return;
    }
    
    float packet_loss_rate = calculate_packet_loss_rate(session);
    size_t old_size = session->buffer_size;
    
    if (packet_loss_rate > 0.05) {
        session->buffer_size = MIN(session->buffer_size * 1.5, RTMP_MAX_BUFFER_SIZE);
    } else if (packet_loss_rate < 0.01) {
        session->buffer_size = MAX(session->buffer_size * 0.8, RTMP_MIN_BUFFER_SIZE);
    }
    
    if (session->buffer_size != old_size) {
        uint8_t *new_buffer = realloc(session->buffer, session->buffer_size);
        if (new_buffer) {
            session->buffer = new_buffer;
        } else {
            // Em caso de falha no realloc, mantém o buffer antigo
            session->buffer_size = old_size;
        }
    }
    
    session->last_buffer_adjustment = current_time;
    session->packet_stats.received = 0;
    session->packet_stats.lost = 0;
}

int rtmp_perform_handshake(rtmp_session_t *session) {
    uint8_t c0c1[1 + RTMP_HANDSHAKE_SIZE];
    uint8_t s0s1[1 + RTMP_HANDSHAKE_SIZE];
    uint8_t c2[RTMP_HANDSHAKE_SIZE];
    uint8_t s2[RTMP_HANDSHAKE_SIZE];
    
    // Recebe C0 e C1
    if (rtmp_read(session, c0c1, sizeof(c0c1)) != sizeof(c0c1)) {
        return -1;
    }
    
    // Verifica versão do protocolo
    if (c0c1[0] != RTMP_VERSION) {
        return -1;
    }
    
    // Prepara S0 e S1
    s0s1[0] = RTMP_VERSION;
    prepare_complex_handshake(s0s1 + 1, (uint8_t*)GENUINE_FMS_KEY, true);
    
    // Envia S0 e S1
    if (rtmp_write(session, s0s1, sizeof(s0s1)) != sizeof(s0s1)) {
        return -1;
    }
    
    // Prepara e envia S2
    prepare_complex_handshake(s2, (uint8_t*)GENUINE_FMS_KEY, true);
    if (rtmp_write(session, s2, sizeof(s2)) != sizeof(s2)) {
        return -1;
    }
    
    // Recebe C2
    if (rtmp_read(session, c2, sizeof(c2)) != sizeof(c2)) {
        return -1;
    }
    
    // Verifica C2
    if (!verify_complex_handshake(c2, (uint8_t*)GENUINE_FP_KEY)) {
        return -1;
    }
    
    // Inicializa o buffer adaptativo
    rtmp_init_adaptive_buffer(session);
    
    return 0;
}

void rtmp_handshake_cleanup(rtmp_session_t *session) {
    if (session->buffer) {
        free(session->buffer);
        session->buffer = NULL;
    }
    session->buffer_size = 0;
    session->adaptive_buffer_enabled = false;
}