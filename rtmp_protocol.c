// rtmp_protocol.c
#include "rtmp_core.h"
#include "rtmp_protocol.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RTMP_HANDSHAKE_SIZE 1536

int rtmp_handshake_perform(RTMPSession* session) {
    uint8_t c0c1[RTMP_HANDSHAKE_SIZE + 1];
    uint8_t s0s1s2[RTMP_HANDSHAKE_SIZE * 2 + 1];
    
    rtmp_log(RTMP_LOG_DEBUG, "Starting RTMP handshake");

    // Recebe C0 e C1
    if (recv(session->socket_fd, c0c1, RTMP_HANDSHAKE_SIZE + 1, 0) != RTMP_HANDSHAKE_SIZE + 1) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to receive C0C1");
        return RTMP_ERROR_HANDSHAKE;
    }

    // Verifica versão do protocolo
    if (c0c1[0] != 3) {
        rtmp_log(RTMP_LOG_ERROR, "Unsupported RTMP version: %d", c0c1[0]);
        return RTMP_ERROR_HANDSHAKE;
    }

    // Prepara S0+S1+S2
    s0s1s2[0] = 3;  // S0 - versão RTMP

    // S1: timestamp + zeros + random bytes
    uint32_t timestamp = (uint32_t)time(NULL);
    memcpy(s0s1s2 + 1, &timestamp, 4);
    memset(s0s1s2 + 5, 0, 4);
    
    // Random bytes para resto do S1
    for (int i = 9; i < RTMP_HANDSHAKE_SIZE + 1; i++) {
        s0s1s2[i] = rand() % 256;
    }
    
    // S2: eco do C1
    memcpy(s0s1s2 + RTMP_HANDSHAKE_SIZE + 1, c0c1 + 1, RTMP_HANDSHAKE_SIZE);

    // Envia S0+S1+S2
    if (send(session->socket_fd, s0s1s2, sizeof(s0s1s2), 0) != sizeof(s0s1s2)) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to send S0S1S2");
        return RTMP_ERROR_HANDSHAKE;
    }

    // Recebe C2
    uint8_t c2[RTMP_HANDSHAKE_SIZE];
    if (recv(session->socket_fd, c2, RTMP_HANDSHAKE_SIZE, 0) != RTMP_HANDSHAKE_SIZE) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to receive C2");
        return RTMP_ERROR_HANDSHAKE;
    }

    rtmp_log(RTMP_LOG_INFO, "RTMP handshake completed successfully");
    return RTMP_OK;
}

int rtmp_protocol_parse_message(uint8_t* data, size_t len, RTMPMessage* message) {
    if (!data || !message || len < 11) {
        return RTMP_ERROR_PROTOCOL;
    }

    message->type = data[0];
    memcpy(&message->timestamp, data + 1, 3);
    memcpy(&message->message_length, data + 4, 3);
    message->message_type_id = data[7];
    memcpy(&message->stream_id, data + 8, 4);

    if (len > 12) {
        message->payload = malloc(len - 12);
        if (!message->payload) {
            return RTMP_ERROR_MEMORY;
        }
        memcpy(message->payload, data + 12, len - 12);
    } else {
        message->payload = NULL;
    }

    return RTMP_OK;
}

int rtmp_protocol_create_message(RTMPMessage* message, uint8_t* buffer, size_t* len) {
    if (!message || !buffer || !len) {
        return RTMP_ERROR_PROTOCOL;
    }

    if (*len < 12 + (message->payload ? message->message_length : 0)) {
        return RTMP_ERROR_PROTOCOL;
    }

    buffer[0] = message->type;
    memcpy(buffer + 1, &message->timestamp, 3);
    memcpy(buffer + 4, &message->message_length, 3);
    buffer[7] = message->message_type_id;
    memcpy(buffer + 8, &message->stream_id, 4);

    if (message->payload && message->message_length > 0) {
        memcpy(buffer + 12, message->payload, message->message_length);
    }

    *len = 12 + (message->payload ? message->message_length : 0);
    return RTMP_OK;
}

void rtmp_protocol_handle_message(RTMPSession* session, RTMPMessage* message) {
    if (!session || !message) return;

    switch (message->message_type_id) {
        case RTMP_MSG_SET_CHUNK_SIZE:
            if (message->payload && message->message_length >= 4) {
                uint32_t chunk_size;
                memcpy(&chunk_size, message->payload, 4);
                session->chunk_size = ntohl(chunk_size);
                rtmp_log(RTMP_LOG_DEBUG, "Set chunk size: %u", session->chunk_size);
            }
            break;

        case RTMP_MSG_WINDOW_ACK_SIZE:
            if (message->payload && message->message_length >= 4) {
                uint32_t window_size;
                memcpy(&window_size, message->payload, 4);
                session->window_size = ntohl(window_size);
                rtmp_log(RTMP_LOG_DEBUG, "Set window ack size: %u", session->window_size);
            }
            break;

        case RTMP_MSG_VIDEO:
            rtmp_log(RTMP_LOG_DEBUG, "Received video data: %u bytes", message->message_length);
            break;

        case RTMP_MSG_AUDIO:
            rtmp_log(RTMP_LOG_DEBUG, "Received audio data: %u bytes", message->message_length);
            break;

        default:
            rtmp_log(RTMP_LOG_DEBUG, "Unhandled message type: %u", message->message_type_id);
            break;
    }
}

void rtmp_message_free(RTMPMessage* message) {
    if (message) {
        free(message->payload);
        message->payload = NULL;
    }
}