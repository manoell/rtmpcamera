#include "rtmp_core.h"
#include "rtmp_protocol.h"
#include "rtmp_handshake.h"
#include <string.h>

// Estrutura para o cabeçalho do chunk RTMP
typedef struct {
    uint8_t fmt;       // Formato do chunk (0-3)
    uint32_t csid;     // Chunk Stream ID
    uint32_t timestamp; // Timestamp
    uint32_t length;   // Comprimento da mensagem
    uint8_t type;      // Tipo da mensagem
    uint32_t stream_id; // Stream ID
} RTMPChunkHeader;

// Função helper para ler bytes exatos
static int read_exact(RTMPClient* client, uint8_t* buffer, size_t size) {
    size_t total = 0;
    while (total < size) {
        ssize_t received = recv(client->socket_fd, buffer + total, size - total, 0);
        if (received <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000); // 1ms delay
                continue;
            }
            return RTMP_ERROR_SOCKET;
        }
        total += received;
    }
    return RTMP_OK;
}

// Função helper para enviar bytes exatos
static int write_exact(RTMPClient* client, const uint8_t* buffer, size_t size) {
    size_t total = 0;
    while (total < size) {
        ssize_t sent = send(client->socket_fd, buffer + total, size - total, 0);
        if (sent <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000); // 1ms delay
                continue;
            }
            return RTMP_ERROR_SOCKET;
        }
        total += sent;
    }
    return RTMP_OK;
}

// Lê o cabeçalho do chunk
static int read_chunk_header(RTMPClient* client, RTMPChunkHeader* header) {
    uint8_t basic_header;
    if (read_exact(client, &basic_header, 1) != RTMP_OK) {
        return RTMP_ERROR_SOCKET;
    }

    // Parse do cabeçalho básico
    header->fmt = (basic_header >> 6) & 0x03;
    header->csid = basic_header & 0x3F;

    // CSID estendido
    if (header->csid == 0) {
        uint8_t ext;
        if (read_exact(client, &ext, 1) != RTMP_OK) {
            return RTMP_ERROR_SOCKET;
        }
        header->csid = ext + 64;
    } else if (header->csid == 1) {
        uint8_t ext[2];
        if (read_exact(client, ext, 2) != RTMP_OK) {
            return RTMP_ERROR_SOCKET;
        }
        header->csid = (ext[1] << 8) + ext[0] + 64;
    }

    // Tamanho do cabeçalho baseado no formato
    int header_size = 0;
    switch (header->fmt) {
        case 0: header_size = 11; break;
        case 1: header_size = 7; break;
        case 2: header_size = 3; break;
        case 3: header_size = 0; break;
    }

    if (header_size > 0) {
        uint8_t header_data[11];
        if (read_exact(client, header_data, header_size) != RTMP_OK) {
            return RTMP_ERROR_SOCKET;
        }

        if (header->fmt <= 2) {
            // Timestamp
            header->timestamp = (header_data[0] << 16) | (header_data[1] << 8) | header_data[2];

            if (header->fmt <= 1) {
                // Message Length & Type
                header->length = (header_data[3] << 16) | (header_data[4] << 8) | header_data[5];
                header->type = header_data[6];

                if (header->fmt == 0) {
                    // Message Stream ID
                    header->stream_id = (header_data[7] << 24) | (header_data[8] << 16) |
                                      (header_data[9] << 8) | header_data[10];
                }
            }
        }
    }

    rtmp_log(RTMP_LOG_DEBUG, "Chunk header: fmt=%d, csid=%d, type=%d, len=%d", 
             header->fmt, header->csid, header->type, header->length);

    return RTMP_OK;
}

// Envia uma mensagem RTMP
int rtmp_protocol_send_message(RTMPClient* client, const RTMPMessage* message) {
    if (!client || !message) return RTMP_ERROR_PROTOCOL;

    // Cabeçalho do chunk
    uint8_t header[12];
    header[0] = (0 << 6) | 2; // fmt 0, csid 2
    
    // Timestamp
    header[1] = (message->timestamp >> 16) & 0xFF;
    header[2] = (message->timestamp >> 8) & 0xFF;
    header[3] = message->timestamp & 0xFF;
    
    // Message Length
    header[4] = (message->message_length >> 16) & 0xFF;
    header[5] = (message->message_length >> 8) & 0xFF;
    header[6] = message->message_length & 0xFF;
    
    // Message Type
    header[7] = message->message_type_id;
    
    // Message Stream ID
    header[8] = (message->stream_id >> 24) & 0xFF;
    header[9] = (message->stream_id >> 16) & 0xFF;
    header[10] = (message->stream_id >> 8) & 0xFF;
    header[11] = message->stream_id & 0xFF;

    // Enviar cabeçalho
    if (write_exact(client, header, sizeof(header)) != RTMP_OK) {
        return RTMP_ERROR_SOCKET;
    }

    // Enviar payload em chunks
    size_t remaining = message->message_length;
    size_t offset = 0;
    while (remaining > 0) {
        size_t chunk_size = remaining > client->chunk_size ? client->chunk_size : remaining;
        if (write_exact(client, message->payload + offset, chunk_size) != RTMP_OK) {
            return RTMP_ERROR_SOCKET;
        }
        remaining -= chunk_size;
        offset += chunk_size;

        // Se ainda há dados, enviar cabeçalho de continuação
        if (remaining > 0) {
            uint8_t continuation = (3 << 6) | 2; // fmt 3, csid 2
            if (write_exact(client, &continuation, 1) != RTMP_OK) {
                return RTMP_ERROR_SOCKET;
            }
        }
    }

    return RTMP_OK;
}

// Lê uma mensagem RTMP completa
int rtmp_protocol_read_message(RTMPClient* client, RTMPMessage* message) {
    if (!client || !message) return RTMP_ERROR_PROTOCOL;

    RTMPChunkHeader header;
    int ret = read_chunk_header(client, &header);
    if (ret != RTMP_OK) {
        return ret;
    }

    // Alocar buffer para a mensagem
    message->message_type_id = header.type;
    message->message_length = header.length;
    message->timestamp = header.timestamp;
    message->stream_id = header.stream_id;
    message->payload = malloc(header.length);

    if (!message->payload) {
        return RTMP_ERROR_MEMORY;
    }

    // Ler payload em chunks
    size_t remaining = header.length;
    size_t offset = 0;
    while (remaining > 0) {
        size_t chunk_size = remaining > client->chunk_size ? client->chunk_size : remaining;
        if (read_exact(client, message->payload + offset, chunk_size) != RTMP_OK) {
            free(message->payload);
            return RTMP_ERROR_SOCKET;
        }
        remaining -= chunk_size;
        offset += chunk_size;

        // Se ainda há dados, ler cabeçalho de continuação
        if (remaining > 0) {
            uint8_t continuation;
            if (read_exact(client, &continuation, 1) != RTMP_OK) {
                free(message->payload);
                return RTMP_ERROR_SOCKET;
            }
        }
    }

    message->payload_size = header.length;
    return RTMP_OK;
}

// Libera recursos da mensagem
void rtmp_message_free(RTMPMessage* message) {
    if (message) {
        free(message->payload);
        message->payload = NULL;
    }
}