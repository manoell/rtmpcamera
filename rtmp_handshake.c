#include "rtmp_core.h"
#include <time.h>
#include <sys/time.h>

int rtmp_handshake_handle(RTMPClient *client) {
    if (!client) return RTMP_ERROR_MEMORY;
    
    rtmp_log(RTMP_LOG_DEBUG, "Starting handshake...");
    
    // Buffer para C0C1 (1 + 1536 bytes)
    uint8_t c0c1[1537];
    ssize_t bytes = read_with_timeout(client->socket_fd, c0c1, sizeof(c0c1), 5000);
    
    if (bytes != sizeof(c0c1)) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to receive C0C1: received %zd bytes", bytes);
        return RTMP_ERROR_HANDSHAKE;
    }
    
    rtmp_log(RTMP_LOG_DEBUG, "Received C0C1: %zd bytes", bytes);
    
    // Verificar versão RTMP (C0)
    if (c0c1[0] != 3) {
        rtmp_log(RTMP_LOG_ERROR, "Unsupported RTMP version: %d", c0c1[0]);
        return RTMP_ERROR_PROTOCOL;
    }
    
    // Criar S0S1S2
    uint8_t s0s1s2[3073] = {0};  // 1 + 1536 + 1536
    
    // S0
    s0s1s2[0] = 3;  // RTMP version
    
    // S1: timestamp + zeros + random
    uint32_t timestamp = (uint32_t)time(NULL);
    memcpy(s0s1s2 + 1, &timestamp, 4);
    memset(s0s1s2 + 5, 0, 4);
    
    // Gerar bytes aleatórios para o resto de S1
    for (int i = 9; i < 1537; i++) {
        s0s1s2[i] = rand() % 256;
    }
    
    // S2: eco do C1
    memcpy(s0s1s2 + 1537, c0c1 + 1, 1536);
    
    // Enviar S0S1S2
    bytes = write_with_timeout(client->socket_fd, s0s1s2, sizeof(s0s1s2), 5000);
    if (bytes != sizeof(s0s1s2)) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to send S0S1S2: sent %zd bytes", bytes);
        return RTMP_ERROR_HANDSHAKE;
    }
    
    rtmp_log(RTMP_LOG_DEBUG, "S0S1S2 sent successfully: %zd bytes", bytes);
    
    // Receber C2
    uint8_t c2[1536];
    bytes = read_with_timeout(client->socket_fd, c2, sizeof(c2), 5000);
    
    if (bytes != sizeof(c2)) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to receive C2: received %zd bytes", bytes);
        return RTMP_ERROR_HANDSHAKE;
    }
    
    rtmp_log(RTMP_LOG_DEBUG, "C2 received: %zd bytes, handshake completed", bytes);
    
    // Handshake completo
    client->state = RTMP_STATE_CONNECT;
    return RTMP_OK;
}