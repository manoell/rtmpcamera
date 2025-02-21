#include "rtmp_chunk.h"
#include "rtmp_utils.h"
#include <string.h>

int rtmp_read_chunk_header(RTMPClient *client, RTMPChunkHeader *header) {
    if (!client || !header) return RTMP_ERROR_PROTOCOL;

    uint8_t basic_header;
    if (read_with_timeout(client->socket_fd, &basic_header, 1, 5000) != 1) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to read basic header");
        return RTMP_ERROR_PROTOCOL;
    }

    // Parse formato e chunk stream ID
    header->fmt = (basic_header >> 6) & 0x03;
    header->csid = basic_header & 0x3F;

    // CSID adicional para valores especiais
    if (header->csid == 0) {
        uint8_t byte;
        if (read_with_timeout(client->socket_fd, &byte, 1, 5000) != 1) {
            return RTMP_ERROR_PROTOCOL;
        }
        header->csid = byte + 64;
    } else if (header->csid == 1) {
        uint8_t bytes[2];
        if (read_with_timeout(client->socket_fd, bytes, 2, 5000) != 2) {
            return RTMP_ERROR_PROTOCOL;
        }
        header->csid = (bytes[1] << 8) + bytes[0] + 64;
    }

    rtmp_log(RTMP_LOG_DEBUG, "Chunk header - fmt: %d, csid: %d", header->fmt, header->csid);

    // Tamanho do header baseado no formato
    uint8_t buffer[11] = {0}; // Tamanho máximo do header
    size_t header_size = 0;

    switch (header->fmt) {
        case RTMP_CHUNK_TYPE_0:
            header_size = 11; // timestamp(3) + length(3) + type_id(1) + stream_id(4)
            break;
        case RTMP_CHUNK_TYPE_1:
            header_size = 7;  // timestamp(3) + length(3) + type_id(1)
            break;
        case RTMP_CHUNK_TYPE_2:
            header_size = 3;  // timestamp(3)
            break;
        case RTMP_CHUNK_TYPE_3:
            header_size = 0;  // sem header
            break;
        default:
            rtmp_log(RTMP_LOG_ERROR, "Invalid chunk format: %d", header->fmt);
            return RTMP_ERROR_PROTOCOL;
    }

    if (header_size > 0) {
        if (read_with_timeout(client->socket_fd, buffer, header_size, 5000) != header_size) {
            rtmp_log(RTMP_LOG_ERROR, "Failed to read header");
            return RTMP_ERROR_PROTOCOL;
        }

        // Parse do header baseado no formato
        if (header->fmt <= RTMP_CHUNK_TYPE_2) {
            // timestamp (3 bytes)
            header->timestamp = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];

            if (header->fmt <= RTMP_CHUNK_TYPE_1) {
                // message length (3 bytes) e message type id (1 byte)
                header->length = (buffer[3] << 16) | (buffer[4] << 8) | buffer[5];
                header->type_id = buffer[6];

                if (header->fmt == RTMP_CHUNK_TYPE_0) {
                    // message stream id (4 bytes)
                    header->stream_id = (buffer[7] << 24) | (buffer[8] << 16) |
                                      (buffer[9] << 8) | buffer[10];
                }
            }
        }
    }

    rtmp_log(RTMP_LOG_DEBUG, "Chunk header - type_id: %d, length: %d, stream_id: %d",
             header->type_id, header->length, header->stream_id);

    return RTMP_OK;
}