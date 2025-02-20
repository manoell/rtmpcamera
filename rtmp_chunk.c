// rtmp_chunk.c
#include "rtmp_chunk.h"
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

RTMPChunkStream* rtmp_chunk_stream_create(int socket_fd) {
    RTMPChunkStream* cs = calloc(1, sizeof(RTMPChunkStream));
    if (!cs) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to allocate chunk stream");
        return NULL;
    }

    cs->chunk_size = RTMP_CHUNK_SIZE_MIN;
    cs->window_size = 2500000; // 2.5MB default window
    cs->bytes_received = 0;
    cs->last_ack = 0;
    cs->socket_fd = socket_fd;

    rtmp_log(RTMP_LOG_DEBUG, "Created chunk stream (size: %u, window: %u)", 
             cs->chunk_size, cs->window_size);
    return cs;
}

void rtmp_chunk_stream_destroy(RTMPChunkStream* cs) {
    if (!cs) return;

    for (int i = 0; i < 64; i++) {
        free(cs->chunks[i].data);
    }
    free(cs);
    rtmp_log(RTMP_LOG_DEBUG, "Destroyed chunk stream");
}

static int read_chunk_basic_header(RTMPChunkStream* cs, uint8_t* fmt, uint32_t* csid) {
    uint8_t basic_header;
    if (recv(cs->socket_fd, &basic_header, 1, 0) != 1) {
        return RTMP_ERROR_SOCKET;
    }

    *fmt = (basic_header >> 6) & 0x03;
    *csid = basic_header & 0x3F;

    if (*csid == 0) {
        uint8_t byte;
        if (recv(cs->socket_fd, &byte, 1, 0) != 1) {
            return RTMP_ERROR_SOCKET;
        }
        *csid = byte + 64;
    } else if (*csid == 1) {
        uint8_t bytes[2];
        if (recv(cs->socket_fd, bytes, 2, 0) != 2) {
            return RTMP_ERROR_SOCKET;
        }
        *csid = (bytes[1] << 8) + bytes[0] + 64;
    }

    return RTMP_OK;
}

static int read_chunk_message_header(RTMPChunkStream* cs, uint8_t fmt, RTMPMessage* message) {
    uint8_t header[11] = {0}; // Tamanho máximo do cabeçalho
    int header_size;

    switch (fmt) {
        case RTMP_CHUNK_TYPE_0:
            header_size = 11;
            break;
        case RTMP_CHUNK_TYPE_1:
            header_size = 7;
            break;
        case RTMP_CHUNK_TYPE_2:
            header_size = 3;
            break;
        case RTMP_CHUNK_TYPE_3:
            header_size = 0;
            break;
        default:
            return RTMP_ERROR_PROTOCOL;
    }

    if (header_size > 0) {
        if (recv(cs->socket_fd, header, header_size, 0) != header_size) {
            return RTMP_ERROR_SOCKET;
        }
    }

    if (fmt <= RTMP_CHUNK_TYPE_2) {
        memcpy(&message->timestamp, header, 3);
        if (fmt <= RTMP_CHUNK_TYPE_1) {
            memcpy(&message->message_length, header + 3, 3);
            message->message_type_id = header[6];
            if (fmt == RTMP_CHUNK_TYPE_0) {
                memcpy(&message->stream_id, header + 7, 4);
            }
        }
    }

    return RTMP_OK;
}

int rtmp_chunk_read(RTMPChunkStream* cs, RTMPMessage* message) {
    if (!cs || !message) {
        return RTMP_ERROR_MEMORY;
    }

    uint8_t fmt;
    uint32_t csid;

    // Lê o cabeçalho básico
    int ret = read_chunk_basic_header(cs, &fmt, &csid);
    if (ret != RTMP_OK) {
        return ret;
    }

    // Lê o cabeçalho da mensagem
    ret = read_chunk_message_header(cs, fmt, message);
    if (ret != RTMP_OK) {
        return ret;
    }

    // Aloca espaço para o payload
    message->payload = malloc(message->message_length);
    if (!message->payload) {
        return RTMP_ERROR_MEMORY;
    }

    // Lê o payload em chunks
    size_t bytes_read = 0;
    while (bytes_read < message->message_length) {
        size_t chunk_size = message->message_length - bytes_read;
        if (chunk_size > cs->chunk_size) {
            chunk_size = cs->chunk_size;
        }

        ssize_t bytes = recv(cs->socket_fd, 
                           message->payload + bytes_read, 
                           chunk_size, 
                           0);
        
        if (bytes <= 0) {
            free(message->payload);
            message->payload = NULL;
            return RTMP_ERROR_SOCKET;
        }

        bytes_read += bytes;
        cs->bytes_received += bytes;

        // Verifica window ack
        if (cs->window_size > 0 && 
            cs->bytes_received - cs->last_ack >= cs->window_size) {
            // TODO: Enviar window ack
            cs->last_ack = cs->bytes_received;
        }
    }

    rtmp_log(RTMP_LOG_DEBUG, "Read chunk: fmt=%d, csid=%u, type=%d, size=%u", 
             fmt, csid, message->message_type_id, message->message_length);

    return RTMP_OK;
}

int rtmp_chunk_write(RTMPChunkStream* cs, RTMPMessage* message) {
    if (!cs || !message) {
        return RTMP_ERROR_MEMORY;
    }

    // Prepara o cabeçalho básico (sempre usa fmt 0 para simplicidade)
    uint8_t basic_header = (RTMP_CHUNK_TYPE_0 << 6);
    if (send(cs->socket_fd, &basic_header, 1, 0) != 1) {
        return RTMP_ERROR_SOCKET;
    }

    // Prepara o cabeçalho da mensagem
    uint8_t header[11] = {0};
    memcpy(header, &message->timestamp, 3);
    memcpy(header + 3, &message->message_length, 3);
    header[6] = message->message_type_id;
    memcpy(header + 7, &message->stream_id, 4);

    if (send(cs->socket_fd, header, 11, 0) != 11) {
        return RTMP_ERROR_SOCKET;
    }

    // Envia o payload em chunks
    size_t bytes_sent = 0;
    while (bytes_sent < message->message_length) {
        size_t chunk_size = message->message_length - bytes_sent;
        if (chunk_size > cs->chunk_size) {
            chunk_size = cs->chunk_size;
        }

        ssize_t bytes = send(cs->socket_fd, 
                           message->payload + bytes_sent, 
                           chunk_size, 
                           0);
        
        if (bytes <= 0) {
            return RTMP_ERROR_SOCKET;
        }

        bytes_sent += bytes;
    }

    rtmp_log(RTMP_LOG_DEBUG, "Wrote chunk: type=%d, size=%u", 
             message->message_type_id, message->message_length);

    return RTMP_OK;
}

void rtmp_chunk_set_size(RTMPChunkStream* cs, uint32_t size) {
    if (!cs) return;
    
    if (size < RTMP_CHUNK_SIZE_MIN) {
        size = RTMP_CHUNK_SIZE_MIN;
    } else if (size > RTMP_CHUNK_SIZE_MAX) {
        size = RTMP_CHUNK_SIZE_MAX;
    }
    
    cs->chunk_size = size;
    rtmp_log(RTMP_LOG_DEBUG, "Set chunk size to %u", size);
}

void rtmp_chunk_update_window(RTMPChunkStream* cs, uint32_t size) {
    if (!cs) return;
    cs->window_size = size;
    rtmp_log(RTMP_LOG_DEBUG, "Updated window size to %u", size);
}