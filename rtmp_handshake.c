#include "rtmp_handshake.h"
#include "rtmp_log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>

int rtmp_handshake_init(rtmp_session_t* session) {
    if (!session) return -1;
    
    session->state = RTMP_STATE_HANDSHAKE_C0C1;
    log_rtmp_level(RTMP_LOG_DEBUG, "Handshake inicializado");
    return 0;
}

int rtmp_process_handshake_c0c1(rtmp_session_t* session, uint8_t* data, uint32_t size) {
    if (!session || !data || size < RTMP_HANDSHAKE_SIZE + 1) {
        log_rtmp_level(RTMP_LOG_ERROR, "Dados de handshake C0/C1 inválidos");
        return -1;
    }

    // Verificar versão (C0)
    if (data[0] != 3) {
        log_rtmp_level(RTMP_LOG_ERROR, "Versão RTMP não suportada: %d", data[0]);
        return -1;
    }

    // Processar C1 enviando S0/S1/S2
    if (rtmp_send_handshake_s0s1s2(session, data + 1) < 0) {
        return -1;
    }

    session->state = RTMP_STATE_HANDSHAKE_C2;
    log_rtmp_level(RTMP_LOG_INFO, "Handshake C0/C1 processado com sucesso");
    return 0;
}

int rtmp_send_handshake_s0s1s2(rtmp_session_t* session, uint8_t* c1_data) {
    uint8_t* response = (uint8_t*)malloc(RTMP_HANDSHAKE_SIZE * 2 + 1);
    if (!response) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao alocar memória para resposta do handshake");
        return -1;
    }

    // S0 - versão
    response[0] = 3;

    // S1 - timestamp e bytes aleatórios
    uint32_t timestamp = (uint32_t)time(NULL);
    memcpy(response + 1, &timestamp, 4);
    memset(response + 5, 0, 4);
    
    // Gerar bytes aleatórios para S1
    for (int i = 9; i < RTMP_HANDSHAKE_SIZE + 1; i++) {
        response[i] = rand() % 256;
    }

    // S2 - echo do C1
    memcpy(response + RTMP_HANDSHAKE_SIZE + 1, c1_data, RTMP_HANDSHAKE_SIZE);

    // Enviar resposta
    if (send(session->socket, response, RTMP_HANDSHAKE_SIZE * 2 + 1, 0) < 0) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao enviar resposta do handshake: %s", strerror(errno));
        free(response);
        return -1;
    }

    free(response);
    log_rtmp_level(RTMP_LOG_INFO, "Handshake S0/S1/S2 enviado");
    return 0;
}

int rtmp_process_handshake_c2(rtmp_session_t* session, uint8_t* data, uint32_t size) {
    if (!session || !data || size < RTMP_HANDSHAKE_SIZE) {
        log_rtmp_level(RTMP_LOG_ERROR, "Dados de handshake C2 inválidos");
        return -1;
    }

    session->state = RTMP_STATE_CONNECTED;
    log_rtmp_level(RTMP_LOG_INFO, "Handshake C2 processado - Handshake completo");
    return 0;
}