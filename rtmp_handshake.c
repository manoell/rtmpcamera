#include "rtmp_core.h"
#include "rtmp_handshake.h"
#include <time.h>
#include <string.h>

#define RTMP_HANDSHAKE_SIZE 1536
#define RTMP_VERSION 3

// Função helper para enviar dados
static int send_bytes(RTMPClient* client, const uint8_t* data, size_t size) {
    ssize_t sent = 0;
    while (sent < size) {
        ssize_t ret = send(client->socket_fd, data + sent, size - sent, 0);
        if (ret <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000);  // 1ms de espera
                continue;
            }
            rtmp_log(RTMP_LOG_ERROR, "Send failed: %s", strerror(errno));
            return RTMP_ERROR_SOCKET;
        }
        sent += ret;
    }
    return RTMP_OK;
}

// Função helper para receber dados
static int receive_bytes(RTMPClient* client, uint8_t* data, size_t size) {
    ssize_t received = 0;
    while (received < size) {
        ssize_t ret = recv(client->socket_fd, data + received, size - received, 0);
        if (ret <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000);  // 1ms de espera
                continue;
            }
            if (ret == 0) {
                rtmp_log(RTMP_LOG_ERROR, "Connection closed during handshake");
            } else {
                rtmp_log(RTMP_LOG_ERROR, "Receive failed: %s", strerror(errno));
            }
            return RTMP_ERROR_SOCKET;
        }
        received += ret;
    }
    return RTMP_OK;
}

// Gera bytes aleatórios para o handshake
static void generate_random_bytes(uint8_t* buffer, size_t size) {
    static bool seeded = false;
    if (!seeded) {
        srand(time(NULL));
        seeded = true;
    }
    
    for (size_t i = 0; i < size; i++) {
        buffer[i] = rand() % 256;
    }
}

int rtmp_handshake_perform(RTMPClient* client) {
    rtmp_log(RTMP_LOG_DEBUG, "Starting handshake...");
    
    // Receber C0 (1 byte versão) + C1 (1536 bytes)
    uint8_t c0c1[RTMP_HANDSHAKE_SIZE + 1];
    if (receive_bytes(client, c0c1, sizeof(c0c1)) != RTMP_OK) {
        return RTMP_ERROR_HANDSHAKE;
    }
    rtmp_log(RTMP_LOG_DEBUG, "Received C0C1: %d bytes", sizeof(c0c1));

    // Verificar versão RTMP
    if (c0c1[0] != RTMP_VERSION) {
        rtmp_log(RTMP_LOG_ERROR, "Unsupported RTMP version: %d", c0c1[0]);
        return RTMP_ERROR_PROTOCOL;
    }

    // Preparar S0 + S1 + S2
    uint8_t s0s1s2[1 + RTMP_HANDSHAKE_SIZE * 2];
    
    // S0 - versão
    s0s1s2[0] = RTMP_VERSION;
    
    // S1 - timestamp (4 bytes) + zeros (4 bytes) + random (1528 bytes)
    uint32_t timestamp = (uint32_t)time(NULL);
    memcpy(s0s1s2 + 1, &timestamp, 4);
    memset(s0s1s2 + 5, 0, 4);
    generate_random_bytes(s0s1s2 + 9, RTMP_HANDSHAKE_SIZE - 8);
    
    // S2 - eco do C1
    memcpy(s0s1s2 + RTMP_HANDSHAKE_SIZE + 1, c0c1 + 1, RTMP_HANDSHAKE_SIZE);

    // Enviar S0 + S1 + S2
    if (send_bytes(client, s0s1s2, sizeof(s0s1s2)) != RTMP_OK) {
        return RTMP_ERROR_HANDSHAKE;
    }
    rtmp_log(RTMP_LOG_DEBUG, "S0S1S2 sent successfully: %d bytes", sizeof(s0s1s2));

    // Receber C2
    uint8_t c2[RTMP_HANDSHAKE_SIZE];
    if (receive_bytes(client, c2, sizeof(c2)) != RTMP_OK) {
        return RTMP_ERROR_HANDSHAKE;
    }
    rtmp_log(RTMP_LOG_DEBUG, "C2 received: %d bytes", sizeof(c2));

    // Verificar C2 (deve ser eco do nosso S1)
    if (memcmp(c2, s0s1s2 + 1, RTMP_HANDSHAKE_SIZE) != 0) {
        rtmp_log(RTMP_LOG_WARNING, "C2 does not match S1 echo");
        // Algumas implementações não seguem o protocolo exatamente,
        // então vamos continuar mesmo assim
    }

    client->state = RTMP_STATE_HANDSHAKE_DONE;
    rtmp_log(RTMP_LOG_INFO, "Handshake completed successfully");
    return RTMP_OK;
}