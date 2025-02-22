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

int rtmp_process_message(rtmp_session_t *session, rtmp_chunk_t *chunk) {
    if (!session || !chunk) return -1;

    switch (chunk->msg_type_id) {
        case RTMP_MSG_SET_CHUNK_SIZE: {
            uint32_t chunk_size;
            memcpy(&chunk_size, chunk->msg_data, 4);
            chunk_size = RTMP_NTOHL(chunk_size);
            session->in_chunk_size = chunk_size;
            return 0;
        }

        case RTMP_MSG_ABORT:
            return rtmp_handle_abort(session, chunk->msg_data, chunk->msg_length);

        case RTMP_MSG_ACKNOWLEDGEMENT: {
            uint32_t sequence_number;
            memcpy(&sequence_number, chunk->msg_data, 4);
            sequence_number = RTMP_NTOHL(sequence_number);
            session->last_ack_received = sequence_number;
            return 0;
        }

        case RTMP_MSG_WINDOW_ACK_SIZE: {
            uint32_t window_size;
            memcpy(&window_size, chunk->msg_data, 4);
            window_size = RTMP_NTOHL(window_size);
            session->window_ack_size = window_size;
            return 0;
        }

        case RTMP_MSG_SET_PEER_BW: {
            uint32_t window_size;
            uint8_t limit_type;
            memcpy(&window_size, chunk->msg_data, 4);
            window_size = RTMP_NTOHL(window_size);
            limit_type = chunk->msg_data[4];
            session->peer_bandwidth = window_size;
            session->peer_bandwidth_limit_type = limit_type;
            return rtmp_send_window_acknowledgement_size(session, window_size);
        }

        case RTMP_MSG_AUDIO:
            return rtmp_handle_audio(session, chunk->msg_data, chunk->msg_length, chunk->timestamp);

        case RTMP_MSG_VIDEO:
            return rtmp_handle_video(session, chunk->msg_data, chunk->msg_length, chunk->timestamp);

        case RTMP_MSG_DATA_AMF3:
            return rtmp_handle_data(session, chunk->msg_data + 1, chunk->msg_length - 1);

        case RTMP_MSG_DATA_AMF0:
            return rtmp_handle_data(session, chunk->msg_data, chunk->msg_length);

        case RTMP_MSG_COMMAND_AMF3:
            return rtmp_handle_command(session, chunk->msg_data + 1, chunk->msg_length - 1);

        case RTMP_MSG_COMMAND_AMF0:
            return rtmp_handle_command(session, chunk->msg_data, chunk->msg_length);

        case RTMP_MSG_AGGREGATE:
            return rtmp_handle_aggregate(session, chunk->msg_data, chunk->msg_length);

        default:
            // Ignore unknown message types
            return 0;
    }
}

static int rtmp_handle_audio(rtmp_session_t *session, const uint8_t *data, size_t size, uint32_t timestamp) {
    if (!session || !data || !size) return -1;

    // Parse AAC audio data
    uint8_t sound_format = (data[0] >> 4) & 0x0F;
    uint8_t sound_rate = (data[0] >> 2) & 0x03;
    uint8_t sound_size = (data[0] >> 1) & 0x01;
    uint8_t sound_type = data[0] & 0x01;
    uint8_t aac_packet_type = data[1];

    // Handle AAC audio
    if (sound_format == 10) { // AAC
        if (aac_packet_type == 0) { // AAC sequence header
            session->aac_sequence_header = malloc(size - 2);
            session->aac_sequence_header_size = size - 2;
            memcpy(session->aac_sequence_header, data + 2, size - 2);
        } else { // AAC raw data
            // Process AAC frame
            if (session->audio_callback) {
                session->audio_callback(session, data + 2, size - 2, timestamp);
            }
        }
    }

    return 0;
}

static int rtmp_handle_video(rtmp_session_t *session, const uint8_t *data, size_t size, uint32_t timestamp) {
    if (!session || !data || !size) return -1;

    // Parse H.264 video data
    uint8_t frame_type = (data[0] >> 4) & 0x0F;
    uint8_t codec_id = data[0] & 0x0F;
    uint8_t avc_packet_type = data[1];
    
    // Handle H.264 video
    if (codec_id == 7) { // H.264/AVC
        if (avc_packet_type == 0) { // AVC sequence header
            session->avc_sequence_header = malloc(size - 5);
            session->avc_sequence_header_size = size - 5;
            memcpy(session->avc_sequence_header, data + 5, size - 5);
        } else if (avc_packet_type == 1) { // AVC NALU
            // Process H.264 frame
            if (session->video_callback) {
                session->video_callback(session, data + 5, size - 5, timestamp);
            }
        }
    }

    return 0;
}

static int rtmp_handle_aggregate(rtmp_session_t *session, const uint8_t *data, size_t size) {
    if (!session || !data || size < 11) return -1;

    size_t offset = 0;
    while (offset + 11 <= size) {
        uint8_t type = data[offset];
        uint32_t size = RTMP_NTOHL(*(uint32_t*)(data + offset + 1));
        uint32_t timestamp = RTMP_NTOHL(*(uint32_t*)(data + offset + 5));
        uint32_t stream_id = RTMP_NTOHL(*(uint32_t*)(data + offset + 8));

        if (offset + 11 + size > size) break;

        const uint8_t *msg_data = data + offset + 11;
        rtmp_chunk_t chunk = {
            .msg_type_id = type,
            .msg_length = size,
            .timestamp = timestamp,
            .msg_stream_id = stream_id,
            .msg_data = msg_data
        };

        rtmp_process_message(session, &chunk);

        offset += 11 + size + 4; // Including back pointer
    }

    return 0;
}

static int rtmp_handle_data(rtmp_session_t *session, const uint8_t *data, size_t size) {
    if (!session || !data || !size) return -1;

    size_t offset = 0;
    char *data_type = NULL;
    
    // Decode data type string
    if (rtmp_amf_decode_string(data, size, &offset, &data_type) < 0) {
        return -1;
    }

    // Handle different data types
    if (strcmp(data_type, "@setDataFrame") == 0) {
        // Process metadata
        char *metadata_type = NULL;
        if (rtmp_amf_decode_string(data, size, &offset, &metadata_type) == 0) {
            if (strcmp(metadata_type, "onMetaData") == 0) {
                // Store metadata for the stream
                if (session->metadata_callback) {
                    session->metadata_callback(session, data + offset, size - offset);
                }
            }
            free(metadata_type);
        }
    }

    free(data_type);
    return 0;
}

static int rtmp_handle_abort(rtmp_session_t *session, const uint8_t *data, size_t size) {
    if (!session || !data || size < 4) return -1;

    uint32_t chunk_stream_id;
    memcpy(&chunk_stream_id, data, 4);
    chunk_stream_id = RTMP_NTOHL(chunk_stream_id);

    // Clear incomplete chunks for this stream
    rtmp_chunk_stream_t *chunk_stream = rtmp_get_chunk_stream(session, chunk_stream_id);
    if (chunk_stream) {
        chunk_stream->msg_length = 0;
        chunk_stream->msg_type_id = 0;
        chunk_stream->msg_stream_id = 0;
        if (chunk_stream->msg_data) {
            free(chunk_stream->msg_data);
            chunk_stream->msg_data = NULL;
        }
    }

    return 0;
}

int rtmp_send_window_acknowledgement_size(rtmp_session_t *session, uint32_t window_size) {
    if (!session) return -1;

    uint8_t message[4];
    uint32_t be_window_size = RTMP_HTONL(window_size);
    memcpy(message, &be_window_size, 4);

    return rtmp_send_message(session, RTMP_MSG_WINDOW_ACK_SIZE, 0, message, sizeof(message));
}

// Gerenciamento de múltiplas conexões
int rtmp_server_add_connection(rtmp_server_t *server, rtmp_session_t *session) {
    if (!server || !session) return -1;

    pthread_mutex_lock(&server->connections_mutex);
    
    // Verificar limite de conexões
    if (server->num_connections >= RTMP_MAX_CONNECTIONS) {
        pthread_mutex_unlock(&server->connections_mutex);
        return -1;
    }

    // Adicionar nova conexão
    server->connections[server->num_connections++] = session;
    
    pthread_mutex_unlock(&server->connections_mutex);
    return 0;
}

int rtmp_server_remove_connection(rtmp_server_t *server, rtmp_session_t *session) {
    if (!server || !session) return -1;

    pthread_mutex_lock(&server->connections_mutex);
    
    // Procurar e remover conexão
    for (int i = 0; i < server->num_connections; i++) {
        if (server->connections[i] == session) {
            // Mover últimas conexões uma posição para trás
            memmove(&server->connections[i], 
                    &server->connections[i + 1],
                    (server->num_connections - i - 1) * sizeof(rtmp_session_t*));
            server->num_connections--;
            break;
        }
    }
    
    pthread_mutex_unlock(&server->connections_mutex);
    return 0;
}