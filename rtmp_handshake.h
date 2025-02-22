#ifndef RTMP_HANDSHAKE_H
#define RTMP_HANDSHAKE_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include "rtmp_core.h"

// Buffer size constants
#define RTMP_INITIAL_BUFFER_SIZE (256 * 1024)  // 256KB
#define RTMP_MIN_BUFFER_SIZE (128 * 1024)      // 128KB
#define RTMP_MAX_BUFFER_SIZE (2 * 1024 * 1024) // 2MB
#define BUFFER_ADJUSTMENT_INTERVAL 5000         // 5 seconds

// Estrutura para estatísticas de pacotes
typedef struct {
    uint32_t received;
    uint32_t lost;
} rtmp_packet_stats_t;

// Extensão da estrutura rtmp_session_t
typedef struct {
    // ... outros campos existentes ...
    
    // Buffer adaptativo
    uint8_t *buffer;
    size_t buffer_size;
    bool adaptive_buffer_enabled;
    uint32_t last_buffer_adjustment;
    rtmp_packet_stats_t packet_stats;
} rtmp_session_t;

// Funções principais
int rtmp_perform_handshake(rtmp_session_t *session);
void rtmp_init_adaptive_buffer(rtmp_session_t *session);
void rtmp_adjust_buffer(rtmp_session_t *session);
void rtmp_handshake_cleanup(rtmp_session_t *session);

// Funções auxiliares
float calculate_packet_loss_rate(rtmp_session_t *session);

#endif // RTMP_HANDSHAKE_H