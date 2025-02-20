// rtmp_protocol.c
#include "rtmp_protocol.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define RTMP_HANDSHAKE_SIZE 1536
#define RTMP_VERSION 3

static uint32_t get_timestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

int rtmp_handshake_perform(RTMPSession* session) {
    uint8_t c0c1[RTMP_HANDSHAKE_SIZE + 1];
    uint8_t s0s1s2[RTMP_HANDSHAKE_SIZE * 2 + 1];
    
    rtmp_log(RTMP_LOG_DEBUG, "Starting RTMP handshake");

    // Recebe C0 e C1
    if (recv(session->socket_fd, c0c1, RTMP_HANDSHAKE_SIZE + 1, 0) != RTMP_HANDSHAKE_SIZE + 1) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to receive C0C1");
        return RTMP_ERROR_HANDSHAKE;
    }

    // Verifica versão
    if (c0c1[0] != RTMP_VERSION) {
        rtmp_log(RTMP_LOG_ERROR, "Unsupported RTMP version: %d", c0c1[0]);
        return RTMP_ERROR_HANDSHAKE;
    }

    // Prepara S0, S1 e S2
    s0s1s2[0] = RTMP_VERSION;  // S0
    
    // S1: timestamp + zeros + random bytes
    uint32_t timestamp = get_timestamp();
    memcpy(s0s1s2 + 1, &timestamp, 4);
    memset(s0s1s2 + 5, 0, 4);
    for (int i = 9; i < RTMP_HANDSHAKE_SIZE + 1; i++) {
        s0s1s2[i] = rand() % 256;
    }
    
    // S2: copia C1
    memcpy(s0s1s2 + RTMP_HANDSHAKE_SIZE + 1, c0c1 + 1, RTMP_HANDSHAKE_SIZE);

    // Envia S0, S1 e S2
    if (send(session->socket_fd, s0s1s2, RTMP_HANDSHAKE_SIZE * 2 + 1, 0) != RTMP_HANDSHAKE_SIZE * 2 + 1) {
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
    if (!data || !message || len < 1) {
        return RTMP_ERROR_MEMORY;
    }

    // Parse basic header
    message->type = data[0] & 0x3F;
    
    // Parse message header
    size_t offset = 1;
    memcpy(&message->timestamp, data + offset, 4);
    offset += 4;
    memcpy(&message->message_length, data + offset, 3);
    offset += 3;
    message->message_type_id = data[offset++];
    memcpy(&message->stream_id, data + offset, 4);
    offset += 4;

    // Copy payload
    size_t payload_size = len - offset;
    message->payload = malloc(payload_size);
    if (!message->payload) {
        return RTMP_ERROR_MEMORY;
    }
    memcpy(message->payload, data + offset, payload_size);

    return RTMP_OK;
}

int rtmp_protocol_create_message(RTMPMessage* message, uint8_t* buffer, size_t* len) {
    if (!message || !buffer || !len) {
        return RTMP_ERROR_MEMORY;
    }

    size_t total_len = 12 + message->message_length; // Header + payload
    if (*len < total_len) {
        return RTMP_ERROR_MEMORY;
    }

    // Write header
    buffer[0] = message->type;
    memcpy(buffer + 1, &message->timestamp, 4);
    memcpy(buffer + 5, &message->message_length, 3);
    buffer[8] = message->message_type_id;
    memcpy(buffer + 9, &message->stream_id, 4);

    // Write payload
    memcpy(buffer + 13, message->payload, message->message_length);
    *len = total_len;

    return RTMP_OK;
}

void rtmp_protocol_handle_message(RTMPSession* session, RTMPMessage* message) {
    switch (message->message_type_id) {
        case RTMP_MSG_VIDEO:
            rtmp_log(RTMP_LOG_DEBUG, "Received video message (size: %u)", message->message_length);
            // Log video stats for analysis
            rtmp_log(RTMP_LOG_INFO, "Video Frame - Size: %u bytes, Timestamp: %u", 
                     message->message_length, message->timestamp);
            break;

        case RTMP_MSG_AUDIO:
            rtmp_log(RTMP_LOG_DEBUG, "Received audio message (size: %u)", message->message_length);
            break;

        case RTMP_MSG_DATA_AMF0:
        case RTMP_MSG_DATA_AMF3:
            rtmp_log(RTMP_LOG_DEBUG, "Received metadata message");
            break;

        default:
            rtmp_log(RTMP_LOG_DEBUG, "Received message type: %d", message->message_type_id);
            break;
    }
}

void rtmp_message_free(RTMPMessage* message) {
    if (message) {
        free(message->payload);
        message->payload = NULL;
    }
}