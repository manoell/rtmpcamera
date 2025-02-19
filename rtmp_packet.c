#include "rtmp_packet.h"
#include "rtmp_log.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

rtmp_packet_t* rtmp_packet_create(void) {
    rtmp_packet_t* packet = (rtmp_packet_t*)calloc(1, sizeof(rtmp_packet_t));
    if (!packet) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao alocar pacote RTMP");
        return NULL;
    }
    return packet;
}

void rtmp_packet_destroy(rtmp_packet_t* packet) {
    if (!packet) return;
    if (packet->data) {
        free(packet->data);
    }
    free(packet);
}

int rtmp_packet_parse(rtmp_session_t* session, uint8_t* data, uint32_t size, rtmp_packet_t* packet) {
    if (!session || !data || !packet || size == 0) return -1;

    uint32_t offset = 0;

    // Processar cabeçalho do chunk
    uint8_t chunk_header = data[offset++];
    uint8_t chunk_type = chunk_header >> 6;
    //uint8_t chunk_stream_id = chunk_header & 0x3F;

    // Decodificar timestamp
    if (chunk_type <= 2) {
        if (size - offset < 3) return -1;
        packet->timestamp = (data[offset] << 16) | (data[offset+1] << 8) | data[offset+2];
        offset += 3;
    }

    // Decodificar tamanho da mensagem
    if (chunk_type <= 1) {
        if (size - offset < 3) return -1;
        packet->size = (data[offset] << 16) | (data[offset+1] << 8) | data[offset+2];
        offset += 3;
        if (size - offset < 1) return -1;
        packet->type = data[offset++];
    }

    // Decodificar stream ID
    if (chunk_type == 0) {
        if (size - offset < 4) return -1;
        packet->stream_id = (data[offset] << 24) | (data[offset+1] << 16) |
                           (data[offset+2] << 8) | data[offset+3];
        offset += 4;
    }

    // Copiar dados do pacote
    uint32_t data_size = size - offset;
    if (data_size > 0) {
        packet->data = (uint8_t*)malloc(data_size);
        if (!packet->data) {
            log_rtmp_level(RTMP_LOG_ERROR, "Falha ao alocar dados do pacote");
            return -1;
        }
        memcpy(packet->data, data + offset, data_size);
        packet->data_size = data_size;
    }

    return 0;
}

int rtmp_packet_serialize(rtmp_packet_t* packet, uint8_t* buffer, uint32_t buffer_size) {
    if (!packet || !buffer) return -1;

    uint32_t offset = 0;
    uint32_t required_size = 12 + packet->data_size; // Cabeçalho básico + dados

    if (buffer_size < required_size) {
        log_rtmp_level(RTMP_LOG_ERROR, "Buffer insuficiente para serializar pacote");
        return -1;
    }

    // Chunk basic header (1 byte)
    buffer[offset++] = 0x03; // Chunk stream ID 3

    // Timestamp (3 bytes)
    buffer[offset++] = (packet->timestamp >> 16) & 0xFF;
    buffer[offset++] = (packet->timestamp >> 8) & 0xFF;
    buffer[offset++] = packet->timestamp & 0xFF;

    // Message length (3 bytes)
    buffer[offset++] = (packet->data_size >> 16) & 0xFF;
    buffer[offset++] = (packet->data_size >> 8) & 0xFF;
    buffer[offset++] = packet->data_size & 0xFF;

    // Message type ID (1 byte)
    buffer[offset++] = packet->type;

    // Message stream ID (4 bytes, little endian)
    buffer[offset++] = packet->stream_id & 0xFF;
    buffer[offset++] = (packet->stream_id >> 8) & 0xFF;
    buffer[offset++] = (packet->stream_id >> 16) & 0xFF;
    buffer[offset++] = (packet->stream_id >> 24) & 0xFF;

    // Copiar dados
    if (packet->data && packet->data_size > 0) {
        memcpy(buffer + offset, packet->data, packet->data_size);
        offset += packet->data_size;
    }

    return offset;
}

int rtmp_send_control_packet(rtmp_session_t* session, uint8_t type, uint32_t value) {
    uint8_t buffer[8];
    uint32_t size = 0;

    switch (type) {
        case RTMP_MSG_ACK:
        case RTMP_MSG_WINDOW_ACK_SIZE:
            buffer[0] = (value >> 24) & 0xFF;
            buffer[1] = (value >> 16) & 0xFF;
            buffer[2] = (value >> 8) & 0xFF;
            buffer[3] = value & 0xFF;
            size = 4;
            break;

        case RTMP_MSG_SET_CHUNK_SIZE:
            buffer[0] = (value >> 24) & 0xFF;
            buffer[1] = (value >> 16) & 0xFF;
            buffer[2] = (value >> 8) & 0xFF;
            buffer[3] = value & 0xFF;
            size = 4;
            break;

        default:
            log_rtmp_level(RTMP_LOG_ERROR, "Tipo de pacote de controle não suportado: %d", type);
            return -1;
    }

    rtmp_packet_t packet = {0};
    packet.type = type;
    packet.data = buffer;
    packet.data_size = size;
    packet.timestamp = 0;
    packet.stream_id = 0;

    return rtmp_packet_send(session, &packet);
}

int rtmp_send_ping(rtmp_session_t* session) {
    uint8_t ping_data[6] = {
        0x00, 0x06,             // User Control Message Events - Ping
        0x00, 0x00, 0x00, 0x00  // Timestamp
    };
    
    rtmp_packet_t packet = {0};
    packet.type = RTMP_MSG_USER_CONTROL;
    packet.data = ping_data;
    packet.data_size = 6;
    packet.timestamp = 0;
    packet.stream_id = 0;

    return rtmp_packet_send(session, &packet);
}

int rtmp_send_ack(rtmp_session_t* session) {
    return rtmp_send_control_packet(session, RTMP_MSG_ACK, session->bytes_in);
}

int rtmp_send_chunk_size(rtmp_session_t* session, uint32_t chunk_size) {
    return rtmp_send_control_packet(session, RTMP_MSG_SET_CHUNK_SIZE, chunk_size);
}

int rtmp_packet_send(rtmp_session_t* session, rtmp_packet_t* packet) {
    if (!session || !packet) return -1;

    uint8_t buffer[RTMP_DEFAULT_BUFFER_SIZE];
    int size = rtmp_packet_serialize(packet, buffer, sizeof(buffer));
    
    if (size < 0) return -1;

    if (send(session->socket, buffer, size, 0) < 0) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao enviar pacote: %s", strerror(errno));
        return -1;
    }

    session->bytes_out += size;
    return 0;
}