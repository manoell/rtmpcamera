#include "rtmp_commands.h"
#include "rtmp_amf.h"
#include <string.h>
#include <errno.h>

static int send_amf_command_response(RTMPClient* client, const char* command_name, 
                                   double transaction_id, const char* description) {
    uint8_t response[1024];  // Buffer grande o suficiente
    size_t offset = 0;
    size_t size;

    // Command name
    size = sizeof(response) - offset;
    if (amf0_encode_string((char*)command_name, response + offset, &size) != RTMP_OK) {
        return RTMP_ERROR_PROTOCOL;
    }
    offset += size;

    // Transaction ID
    size = sizeof(response) - offset;
    if (amf0_encode_number(transaction_id, response + offset, &size) != RTMP_OK) {
        return RTMP_ERROR_PROTOCOL;
    }
    offset += size;

    // Command info object
    response[offset++] = AMF0_OBJECT;

    // Add properties
    const char* props[] = {"level", "code", "description"};
    const char* values[] = {"status", "_Result", description};
    
    for (int i = 0; i < 3; i++) {
        size = sizeof(response) - offset;
        if (amf0_encode_string((char*)props[i], response + offset, &size) != RTMP_OK) {
            return RTMP_ERROR_PROTOCOL;
        }
        offset += size;

        size = sizeof(response) - offset;
        if (amf0_encode_string((char*)values[i], response + offset, &size) != RTMP_OK) {
            return RTMP_ERROR_PROTOCOL;
        }
        offset += size;
    }

    // Object end marker
    response[offset++] = 0x00;
    response[offset++] = 0x00;
    response[offset++] = AMF0_OBJECT_END;

    // Criar header RTMP
    uint8_t header[12] = {
        0x03,  // fmt = 0, csid = 3
        0x00, 0x00, 0x00,  // timestamp
        (offset >> 16) & 0xFF, (offset >> 8) & 0xFF, offset & 0xFF,  // message length
        0x14,  // message type (20 = AMF0 Command)
        0x00, 0x00, 0x00, 0x00  // message stream id
    };

    // Enviar header e payload
    if (write_with_timeout(client->socket_fd, header, sizeof(header), 5000) < 0) {
        return RTMP_ERROR_PROTOCOL;
    }
    if (write_with_timeout(client->socket_fd, response, offset, 5000) < 0) {
        return RTMP_ERROR_PROTOCOL;
    }

    rtmp_log(RTMP_LOG_DEBUG, "Sent command response: %s", command_name);
    return RTMP_OK;
}

static int send_connect_response(RTMPClient* client) {
    // Enviar configurações iniciais
    uint8_t win_ack[] = {
        0x02,  // fmt = 0, csid = 2
        0x00, 0x00, 0x00,  // timestamp
        0x00, 0x00, 0x04,  // message length
        0x05,  // message type (5 = Window Acknowledgement Size)
        0x00, 0x00, 0x00, 0x00,  // stream id
        0x00, 0x26, 0x25, 0xA0  // window size (2.5MB)
    };

    uint8_t set_peer_bw[] = {
        0x02,  // fmt = 0, csid = 2
        0x00, 0x00, 0x00,  // timestamp
        0x00, 0x00, 0x05,  // message length
        0x06,  // message type (6 = Set Peer Bandwidth)
        0x00, 0x00, 0x00, 0x00,  // stream id
        0x00, 0x26, 0x25, 0xA0,  // window size (2.5MB)
        0x02  // dynamic
    };

    uint8_t set_chunk_size[] = {
        0x02,  // fmt = 0, csid = 2
        0x00, 0x00, 0x00,  // timestamp
        0x00, 0x00, 0x04,  // message length
        0x01,  // message type (1 = Set Chunk Size)
        0x00, 0x00, 0x00, 0x00,  // stream id
        0x00, 0x00, 0x10, 0x00  // chunk size (4096)
    };

    if (write_with_timeout(client->socket_fd, win_ack, sizeof(win_ack), 5000) < 0 ||
        write_with_timeout(client->socket_fd, set_peer_bw, sizeof(set_peer_bw), 5000) < 0 ||
        write_with_timeout(client->socket_fd, set_chunk_size, sizeof(set_chunk_size), 5000) < 0) {
        return RTMP_ERROR_PROTOCOL;
    }

    return send_amf_command_response(client, "_result", 1, "Connection succeeded");
}

static int handle_connect_command(RTMPClient* client, uint8_t* data, size_t size) {
    char* command_name;
    uint16_t name_len;
    size_t offset = 0;
    
    // Decode command name
    int ret = amf0_decode_string(data, size, &command_name, &name_len);
    if (ret < 0) return ret;
    offset += ret;

    // Decode transaction ID
    double transaction_id;
    ret = amf0_decode_number(data + offset, size - offset, &transaction_id);
    if (ret < 0) {
        free(command_name);
        return ret;
    }

    rtmp_log(RTMP_LOG_INFO, "Received connect command (transaction_id: %f)", transaction_id);

    free(command_name);
    return send_connect_response(client);
}

static int handle_createStream_command(RTMPClient* client, uint8_t* data, size_t size) {
    char* command_name;
    uint16_t name_len;
    size_t offset = 0;
    
    int ret = amf0_decode_string(data, size, &command_name, &name_len);
    if (ret < 0) return ret;
    offset += ret;

    double transaction_id;
    ret = amf0_decode_number(data + offset, size - offset, &transaction_id);
    if (ret < 0) {
        free(command_name);
        return ret;
    }

    rtmp_log(RTMP_LOG_INFO, "Received createStream command (transaction_id: %f)", transaction_id);

    ret = send_amf_command_response(client, "_result", transaction_id, "Stream created");
    free(command_name);
    return ret;
}

int rtmp_handle_packet(RTMPClient* client, uint8_t* data, size_t size) {
    if (!client || !data || size < 1) return RTMP_ERROR_PROTOCOL;

    RTMPChunkHeader header = {0};
    int ret = rtmp_read_chunk_header(client, &header);
    if (ret != RTMP_OK) return ret;

    // Alocar buffer para o payload
    uint8_t* payload = malloc(header.length);
    if (!payload) return RTMP_ERROR_MEMORY;

    if (read_with_timeout(client->socket_fd, payload, header.length, 5000) != header.length) {
        free(payload);
        return RTMP_ERROR_PROTOCOL;
    }

    switch (header.type_id) {
        case RTMP_MSG_COMMAND_AMF0: {
            // Verificar o tipo de comando
            char* command_name;
            uint16_t name_len;
            if (amf0_decode_string(payload, header.length, &command_name, &name_len) > 0) {
                rtmp_log(RTMP_LOG_DEBUG, "Received command: %s", command_name);
                
                if (strcmp(command_name, "connect") == 0) {
                    ret = handle_connect_command(client, payload, header.length);
                } else if (strcmp(command_name, "createStream") == 0) {
                    ret = handle_createStream_command(client, payload, header.length);
                } else {
                    rtmp_log(RTMP_LOG_DEBUG, "Unknown command: %s", command_name);
                }
                
                free(command_name);
            }
            break;
        }

        case RTMP_MSG_VIDEO:
            rtmp_log(RTMP_LOG_DEBUG, "Received video packet: %u bytes", header.length);
            // TODO: Implementar processamento de vídeo
            break;

        case RTMP_MSG_AUDIO:
            rtmp_log(RTMP_LOG_DEBUG, "Received audio packet: %u bytes", header.length);
            // TODO: Implementar processamento de áudio
            break;

        default:
            rtmp_log(RTMP_LOG_DEBUG, "Unhandled message type: %d", header.type_id);
            break;
    }

    free(payload);
    return ret;
}