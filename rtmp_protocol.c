#include "rtmp_protocol.h"
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// Protocol helper functions
static bool handshake_c0c1(RTMPContext *ctx);
static bool handshake_s0s1s2(RTMPContext *ctx);
static bool handshake_c2(RTMPContext *ctx);
static void handle_control_message(RTMPContext *ctx, RTMPPacket *packet);
static void handle_command_message(RTMPContext *ctx, RTMPPacket *packet);
static bool write_chunk_header(RTMPContext *ctx, RTMPChunkType type, uint32_t timestamp, 
                             uint32_t msgLength, uint8_t msgType, uint32_t msgStreamId);
static bool read_chunk_header(RTMPContext *ctx, RTMPChunkType *type, uint32_t *timestamp,
                            uint32_t *msgLength, uint8_t *msgType, uint32_t *msgStreamId);

// Core implementation
RTMPContext *rtmp_create(void) {
    RTMPContext *ctx = (RTMPContext *)calloc(1, sizeof(RTMPContext));
    if (!ctx) return NULL;
    
    ctx->socket = -1;
    ctx->state = RTMP_STATE_DISCONNECTED;
    ctx->chunkSize = RTMP_CHUNK_SIZE;
    ctx->streamId = 0;
    ctx->numInvokes = 0;
    ctx->windowAckSize = 2500000;
    ctx->bytesReceived = 0;
    ctx->lastAckSize = 0;
    
    ctx->handshakeBuffer = (uint8_t *)malloc(RTMP_HANDSHAKE_SIZE * 2);
    if (!ctx->handshakeBuffer) {
        free(ctx);
        return NULL;
    }
    
    return ctx;
}

void rtmp_destroy(RTMPContext *ctx) {
    if (!ctx) return;
    
    rtmp_disconnect(ctx);
    
    if (ctx->handshakeBuffer) {
        free(ctx->handshakeBuffer);
    }
    
    free(ctx);
}

bool rtmp_connect(RTMPContext *ctx, const char *host, int port) {
    if (!ctx || !host) return false;
    
    // Create socket
    ctx->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->socket < 0) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to create socket");
        return false;
    }
    
    // Set socket options
    int flag = 1;
    setsockopt(ctx->socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    // Connect
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        rtmp_log(RTMP_LOG_ERROR, "Invalid address");
        close(ctx->socket);
        ctx->socket = -1;
        return false;
    }
    
    if (connect(ctx->socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to connect: %s", strerror(errno));
        close(ctx->socket);
        ctx->socket = -1;
        return false;
    }
    
    // Start handshake
    ctx->state = RTMP_STATE_HANDSHAKE_INIT;
    if (!handshake_c0c1(ctx)) {
        rtmp_disconnect(ctx);
        return false;
    }
    
    if (!handshake_s0s1s2(ctx)) {
        rtmp_disconnect(ctx);
        return false;
    }
    
    if (!handshake_c2(ctx)) {
        rtmp_disconnect(ctx);
        return false;
    }
    
    ctx->state = RTMP_STATE_CONNECT;
    return true;
}

void rtmp_disconnect(RTMPContext *ctx) {
    if (!ctx) return;
    
    if (ctx->socket >= 0) {
        close(ctx->socket);
        ctx->socket = -1;
    }
    
    ctx->state = RTMP_STATE_DISCONNECTED;
    ctx->streamId = 0;
    ctx->numInvokes = 0;
    ctx->bytesReceived = 0;
    ctx->lastAckSize = 0;
    
    if (ctx->onStateChange) {
        ctx->onStateChange(ctx, RTMP_STATE_DISCONNECTED);
    }
}

bool rtmp_is_connected(RTMPContext *ctx) {
    return ctx && ctx->socket >= 0 && ctx->state >= RTMP_STATE_CONNECTED;
}

// Handshake implementation
static bool handshake_c0c1(RTMPContext *ctx) {
    uint8_t *c0c1 = ctx->handshakeBuffer;
    
    // C0: version
    c0c1[0] = RTMP_VERSION;
    
    // C1: timestamp, zeros, random bytes
    uint32_t timestamp = rtmp_get_timestamp();
    memcpy(&c0c1[1], &timestamp, 4);
    memset(&c0c1[5], 0, 4);
    
    for (int i = 9; i < RTMP_HANDSHAKE_SIZE + 1; i++) {
        c0c1[i] = rand() & 0xff;
    }
    
    // Send C0+C1
    if (send(ctx->socket, c0c1, RTMP_HANDSHAKE_SIZE + 1, 0) != RTMP_HANDSHAKE_SIZE + 1) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to send C0+C1");
        return false;
    }
    
    return true;
}

static bool handshake_s0s1s2(RTMPContext *ctx) {
    uint8_t *s0s1s2 = ctx->handshakeBuffer;
    int bytesRead = 0;
    int remaining = RTMP_HANDSHAKE_SIZE * 2 + 1;
    
    // Read S0+S1+S2
    while (remaining > 0) {
        int ret = recv(ctx->socket, s0s1s2 + bytesRead, remaining, 0);
        if (ret <= 0) {
            rtmp_log(RTMP_LOG_ERROR, "Failed to receive S0+S1+S2");
            return false;
        }
        bytesRead += ret;
        remaining -= ret;
    }
    
    // Verify S0 version
    if (s0s1s2[0] != RTMP_VERSION) {
        rtmp_log(RTMP_LOG_ERROR, "Invalid RTMP version");
        return false;
    }
    
    return true;
}

static bool handshake_c2(RTMPContext *ctx) {
    uint8_t *c2 = ctx->handshakeBuffer;
    
    // C2: copy S1 timestamp, zeros, random bytes
    memcpy(c2, ctx->handshakeBuffer + 1, RTMP_HANDSHAKE_SIZE);
    
    // Send C2
    if (send(ctx->socket, c2, RTMP_HANDSHAKE_SIZE, 0) != RTMP_HANDSHAKE_SIZE) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to send C2");
        return false;
    }
    
    return true;
}

// Packet handling implementation
bool rtmp_send_packet(RTMPContext *ctx, RTMPPacket *packet) {
    if (!ctx || !packet || ctx->socket < 0) return false;
    
    uint8_t header[16]; // Maximum header size
    int headerSize;
    
    // Write basic header
    header[0] = (packet->type & 0x3f) | (RTMP_CHUNK_TYPE_0 << 6);
    headerSize = 1;
    
    // Write message header based on chunk type
    if (RTMP_CHUNK_TYPE_0) {
        uint32_t timestamp = packet->timestamp;
        header[headerSize++] = (timestamp >> 16) & 0xff;
        header[headerSize++] = (timestamp >> 8) & 0xff;
        header[headerSize++] = timestamp & 0xff;
        
        header[headerSize++] = (packet->size >> 16) & 0xff;
        header[headerSize++] = (packet->size >> 8) & 0xff;
        header[headerSize++] = packet->size & 0xff;
        header[headerSize++] = packet->type;
        
        header[headerSize++] = packet->streamId & 0xff;
        header[headerSize++] = (packet->streamId >> 8) & 0xff;
        header[headerSize++] = (packet->streamId >> 16) & 0xff;
        header[headerSize++] = (packet->streamId >> 24) & 0xff;
    }

    // Send header
    if (send(ctx->socket, header, headerSize, 0) != headerSize) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to send packet header");
        return false;
    }

    // Send payload
    if (packet->size > 0 && packet->data) {
        size_t chunk_size = ctx->chunkSize;
        size_t remaining = packet->size;
        uint8_t *data = packet->data;

        while (remaining > 0) {
            size_t send_size = remaining > chunk_size ? chunk_size : remaining;
            if (send(ctx->socket, data, send_size, 0) != send_size) {
                rtmp_log(RTMP_LOG_ERROR, "Failed to send packet data");
                return false;
            }

            data += send_size;
            remaining -= send_size;

            // Write continuation header if needed
            if (remaining > 0) {
                uint8_t basic_header = (packet->type & 0x3f) | (RTMP_CHUNK_TYPE_3 << 6);
                if (send(ctx->socket, &basic_header, 1, 0) != 1) {
                    rtmp_log(RTMP_LOG_ERROR, "Failed to send continuation header");
                    return false;
                }
            }
        }
    }

    return true;
}

bool rtmp_read_packet(RTMPContext *ctx, RTMPPacket *packet) {
    if (!ctx || !packet || ctx->socket < 0) return false;

    uint8_t header[16];
    int headerSize = 1;

    // Read basic header
    if (recv(ctx->socket, header, 1, 0) != 1) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to read basic header");
        return false;
    }

    uint8_t chunk_type = (header[0] >> 6) & 0x03;
    packet->type = header[0] & 0x3f;

    // Read message header based on chunk type
    switch (chunk_type) {
        case RTMP_CHUNK_TYPE_0:
            headerSize = 12;
            break;
        case RTMP_CHUNK_TYPE_1:
            headerSize = 8;
            break;
        case RTMP_CHUNK_TYPE_2:
            headerSize = 4;
            break;
        case RTMP_CHUNK_TYPE_3:
            headerSize = 1;
            break;
    }

    // Read the rest of header
    if (headerSize > 1) {
        if (recv(ctx->socket, header + 1, headerSize - 1, 0) != headerSize - 1) {
            rtmp_log(RTMP_LOG_ERROR, "Failed to read message header");
            return false;
        }

        if (chunk_type == RTMP_CHUNK_TYPE_0) {
            packet->timestamp = (header[1] << 16) | (header[2] << 8) | header[3];
            packet->size = (header[4] << 16) | (header[5] << 8) | header[6];
            packet->type = header[7];
            packet->streamId = (header[11] << 24) | (header[10] << 16) | 
                             (header[9] << 8) | header[8];
        }
    }

    // Allocate and read payload
    if (packet->size > 0) {
        packet->data = (uint8_t *)malloc(packet->size);
        if (!packet->data) {
            rtmp_log(RTMP_LOG_ERROR, "Failed to allocate packet data");
            return false;
        }

        size_t chunk_size = ctx->chunkSize;
        size_t remaining = packet->size;
        uint8_t *data = packet->data;

        while (remaining > 0) {
            size_t read_size = remaining > chunk_size ? chunk_size : remaining;
            if (recv(ctx->socket, data, read_size, 0) != read_size) {
                rtmp_log(RTMP_LOG_ERROR, "Failed to read packet data");
                free(packet->data);
                packet->data = NULL;
                return false;
            }

            data += read_size;
            remaining -= read_size;

            // Read continuation header if needed
            if (remaining > 0) {
                uint8_t basic_header;
                if (recv(ctx->socket, &basic_header, 1, 0) != 1) {
                    rtmp_log(RTMP_LOG_ERROR, "Failed to read continuation header");
                    free(packet->data);
                    packet->data = NULL;
                    return false;
                }
            }
        }
    }

    // Update bytes received and send acknowledgement if needed
    ctx->bytesReceived += packet->size + headerSize;
    if (ctx->bytesReceived - ctx->lastAckSize >= ctx->windowAckSize) {
        rtmp_send_ack(ctx, ctx->bytesReceived);
        ctx->lastAckSize = ctx->bytesReceived;
    }

    return true;
}

void rtmp_handle_packet(RTMPContext *ctx, RTMPPacket *packet) {
    if (!ctx || !packet) return;

    switch (packet->type) {
        case RTMP_MSG_CHUNK_SIZE:
        case RTMP_MSG_ABORT:
        case RTMP_MSG_ACK:
        case RTMP_MSG_USER_CONTROL:
        case RTMP_MSG_WINDOW_ACK_SIZE:
        case RTMP_MSG_SET_PEER_BW:
            handle_control_message(ctx, packet);
            break;

        case RTMP_MSG_COMMAND_AMF0:
        case RTMP_MSG_COMMAND_AMF3:
            handle_command_message(ctx, packet);
            break;

        case RTMP_MSG_AUDIO:
        case RTMP_MSG_VIDEO:
        case RTMP_MSG_DATA_AMF0:
        case RTMP_MSG_DATA_AMF3:
            if (ctx->onPacket) {
                ctx->onPacket(ctx, packet);
            }
            break;

        default:
            rtmp_log(RTMP_LOG_WARNING, "Unhandled packet type: %d", packet->type);
            break;
    }
}

// Command messages implementation
bool rtmp_send_connect(RTMPContext *ctx) {
    if (!ctx) return false;

    AMFObject *obj = amf_object_create();
    if (!obj) return false;

    // Add command properties
    amf_object_add_string(obj, "app", ctx->settings.app);
    amf_object_add_string(obj, "flashVer", "FMLE/3.0");
    amf_object_add_string(obj, "tcUrl", ctx->settings.tcUrl);
    amf_object_add_boolean(obj, "fpad", false);
    amf_object_add_number(obj, "capabilities", 15.0);
    amf_object_add_number(obj, "audioCodecs", 3191.0);
    amf_object_add_number(obj, "videoCodecs", 252.0);
    amf_object_add_number(obj, "videoFunction", 1.0);

    // Create connect packet
    RTMPPacket packet = {
        .type = RTMP_MSG_COMMAND_AMF0,
        .timestamp = 0,
        .streamId = 0
    };

    // Encode packet data
    uint8_t *data = NULL;
    size_t size = 0;
    bool result = amf_encode_command("connect", ++ctx->numInvokes, obj, &data, &size);
    amf_object_free(obj);

    if (!result) return false;

    packet.data = data;
    packet.size = size;

    // Send packet
    result = rtmp_send_packet(ctx, &packet);
    free(data);

    return result;
}

// Additional command functions would follow similar pattern:
// rtmp_send_create_stream, rtmp_send_publish, rtmp_send_play, etc.

// Control message handlers
static void handle_control_message(RTMPContext *ctx, RTMPPacket *packet) {
    if (!ctx || !packet || !packet->data) return;

    switch (packet->type) {
        case RTMP_MSG_CHUNK_SIZE:
            if (packet->size >= 4) {
                uint32_t chunk_size = (packet->data[0] << 24) | (packet->data[1] << 16) |
                                    (packet->data[2] << 8) | packet->data[3];
                rtmp_set_chunk_size(ctx, chunk_size);
            }
            break;

        case RTMP_MSG_WINDOW_ACK_SIZE:
            if (packet->size >= 4) {
                uint32_t ack_size = (packet->data[0] << 24) | (packet->data[1] << 16) |
                                  (packet->data[2] << 8) | packet->data[3];
                rtmp_set_window_ack_size(ctx, ack_size);
            }
            break;

        // Handle other control messages...
    }
}

static void handle_command_message(RTMPContext *ctx, RTMPPacket *packet) {
    if (!ctx || !packet || !packet->data) return;

    char *command = NULL;
    double transaction_id = 0;
    AMFObject *obj = NULL;

    // Decode command name and transaction ID
    size_t offset = amf_decode_string((char *)packet->data, &command);
    if (!command) return;

    offset += amf_decode_number((char *)packet->data + offset, &transaction_id);
    
    // Handle different commands
    if (strcmp(command, "_result") == 0) {
        // Handle connection response
        if (ctx->state == RTMP_STATE_CONNECT) {
            ctx->state = RTMP_STATE_CONNECTED;
            if (ctx->onStateChange) {
                ctx->onStateChange(ctx, RTMP_STATE_CONNECTED);
            }
        }
    }
    // Handle other commands...

    free(command);
    if (obj) amf_object_free(obj);
}

// Utility implementations
void rtmp_set_chunk_size(RTMPContext *ctx, uint32_t size) {
    if (!ctx) return;
    ctx->chunkSize = size;
}

void rtmp_set_window_ack_size(RTMPContext *ctx, uint32_t size) {
    if (!ctx) return;
    ctx->windowAckSize = size;
}

void rtmp_set_stream_id(RTMPContext *ctx, uint32_t streamId) {
    if (!ctx) return;
    ctx->streamId = streamId;
}