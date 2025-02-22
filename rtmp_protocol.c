#include "rtmp_protocol.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

// Internal protocol context structure
struct rtmp_context_t {
    int socket;
    rtmp_state_t state;
    rtmp_config_t config;
    rtmp_callbacks_t callbacks;
    rtmp_stats_t stats;
    
    // Handshake buffers
    uint8_t *handshake_in;
    uint8_t *handshake_out;
    
    // Chunk processing
    uint32_t chunk_size;
    rtmp_chunk_t **chunks;
    size_t num_chunks;
    
    // Stream management
    uint32_t next_stream_id;
    uint32_t transaction_id;
    
    // Buffer management
    uint8_t *read_buffer;
    size_t read_buffer_size;
    uint8_t *write_buffer;
    size_t write_buffer_size;
    
    // Error handling
    char error_message[256];
};

// Internal utility functions
static void rtmp_set_error(rtmp_context_t *ctx, rtmp_error_t error, const char *fmt, ...);
static rtmp_error_t rtmp_socket_connect(rtmp_context_t *ctx, const char *host, uint16_t port);
static rtmp_error_t rtmp_socket_send(rtmp_context_t *ctx, const uint8_t *data, size_t len);
static rtmp_error_t rtmp_socket_recv(rtmp_context_t *ctx, uint8_t *data, size_t len);
static rtmp_error_t rtmp_process_handshake(rtmp_context_t *ctx);
static rtmp_error_t rtmp_process_chunk(rtmp_context_t *ctx, rtmp_chunk_t *chunk);
static rtmp_error_t rtmp_send_chunk(rtmp_context_t *ctx, rtmp_chunk_t *chunk);

rtmp_context_t* rtmp_create(const rtmp_config_t *config, const rtmp_callbacks_t *callbacks) {
    rtmp_context_t *ctx = (rtmp_context_t*)calloc(1, sizeof(rtmp_context_t));
    if (!ctx) return NULL;
    
    // Initialize config with defaults or provided values
    if (config) {
        ctx->config = *config;
    } else {
        ctx->config.chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
        ctx->config.window_size = 2500000;
        ctx->config.peer_bandwidth = 2500000;
        ctx->config.peer_bandwidth_limit_type = 2;
        ctx->config.tcp_nodelay = true;
        ctx->config.timeout_ms = RTMP_DEFAULT_TIMEOUT;
    }
    
    // Set callbacks
    if (callbacks) {
        ctx->callbacks = *callbacks;
    }
    
    // Initialize buffers
    ctx->handshake_in = (uint8_t*)malloc(RTMP_HANDSHAKE_SIZE);
    ctx->handshake_out = (uint8_t*)malloc(RTMP_HANDSHAKE_SIZE);
    ctx->read_buffer = (uint8_t*)malloc(ctx->config.chunk_size);
    ctx->write_buffer = (uint8_t*)malloc(ctx->config.chunk_size);
    
    if (!ctx->handshake_in || !ctx->handshake_out || 
        !ctx->read_buffer || !ctx->write_buffer) {
        rtmp_destroy(ctx);
        return NULL;
    }
    
    // Initialize state
    ctx->state = RTMP_STATE_UNINITIALIZED;
    ctx->socket = -1;
    ctx->next_stream_id = 1;
    ctx->transaction_id = 1;
    
    return ctx;
}

void rtmp_destroy(rtmp_context_t *ctx) {
    if (!ctx) return;
    
    // Close socket if open
    if (ctx->socket >= 0) {
        close(ctx->socket);
    }
    
    // Free buffers
    free(ctx->handshake_in);
    free(ctx->handshake_out);
    free(ctx->read_buffer);
    free(ctx->write_buffer);
    
    // Free chunks
    if (ctx->chunks) {
        for (size_t i = 0; i < ctx->num_chunks; i++) {
            rtmp_chunk_destroy(ctx->chunks[i]);
        }
        free(ctx->chunks);
    }
    
    free(ctx);
}

rtmp_error_t rtmp_connect(rtmp_context_t *ctx, const char *host, uint16_t port) {
    if (!ctx || !host) {
        return RTMP_ERROR_INVALID_STATE;
    }
    
    // Connect socket
    rtmp_error_t err = rtmp_socket_connect(ctx, host, port);
    if (err != RTMP_ERROR_OK) {
        return err;
    }
    
    // Perform handshake
    err = rtmp_handshake(ctx);
    if (err != RTMP_ERROR_OK) {
        rtmp_disconnect(ctx);
        return err;
    }
    
    // Update state
    ctx->state = RTMP_STATE_CONNECTED;
    if (ctx->callbacks.on_state_change) {
        ctx->callbacks.on_state_change(ctx, RTMP_STATE_HANDSHAKE_DONE, RTMP_STATE_CONNECTED);
    }
    
    return RTMP_ERROR_OK;
}

rtmp_error_t rtmp_disconnect(rtmp_context_t *ctx) {
    if (!ctx) {
        return RTMP_ERROR_INVALID_STATE;
    }
    
    if (ctx->socket >= 0) {
        close(ctx->socket);
        ctx->socket = -1;
    }
    
    rtmp_state_t old_state = ctx->state;
    ctx->state = RTMP_STATE_DISCONNECTED;
    
    if (ctx->callbacks.on_state_change) {
        ctx->callbacks.on_state_change(ctx, old_state, RTMP_STATE_DISCONNECTED);
    }
    
    return RTMP_ERROR_OK;
}

rtmp_error_t rtmp_handshake(rtmp_context_t *ctx) {
    if (!ctx || ctx->state != RTMP_STATE_UNINITIALIZED) {
        return RTMP_ERROR_INVALID_STATE;
    }
    
    // Send C0 (version)
    uint8_t version = RTMP_VERSION;
    if (rtmp_socket_send(ctx, &version, 1) != RTMP_ERROR_OK) {
        return RTMP_ERROR_HANDSHAKE;
    }
    
    // Send C1 (timestamp + zero + random bytes)
    uint32_t timestamp = (uint32_t)time(NULL);
    memset(ctx->handshake_out, 0, RTMP_HANDSHAKE_SIZE);
    memcpy(ctx->handshake_out, &timestamp, 4);
    for (int i = 8; i < RTMP_HANDSHAKE_SIZE; i++) {
        ctx->handshake_out[i] = rand() % 256;
    }
    
    if (rtmp_socket_send(ctx, ctx->handshake_out, RTMP_HANDSHAKE_SIZE) != RTMP_ERROR_OK) {
        return RTMP_ERROR_HANDSHAKE;
    }
    
    ctx->state = RTMP_STATE_VERSION_SENT;
    
    // Process handshake response
    return rtmp_process_handshake(ctx);
}

rtmp_error_t rtmp_send_message(rtmp_context_t *ctx, uint8_t type, uint32_t stream_id, 
                              const uint8_t *data, size_t len) {
    if (!ctx || !data || len == 0) {
        return RTMP_ERROR_INVALID_STATE;
    }
    
    rtmp_chunk_t *chunk = rtmp_chunk_create();
    if (!chunk) {
        return RTMP_ERROR_MEMORY;
    }
    
    // Set chunk header
    rtmp_chunk_set_type(chunk, RTMP_CHUNK_TYPE_0);
    rtmp_chunk_set_message_type(chunk, type);
    rtmp_chunk_set_message_length(chunk, len);
    rtmp_chunk_set_stream_id(chunk, stream_id);
    rtmp_chunk_set_timestamp(chunk, (uint32_t)time(NULL));
    
    // Set chunk data
    if (rtmp_chunk_append_data(chunk, data, len) != RTMP_CHUNK_OK) {
        rtmp_chunk_destroy(chunk);
        return RTMP_ERROR_CHUNK;
    }
    
    // Send chunk
    rtmp_error_t err = rtmp_send_chunk(ctx, chunk);
    rtmp_chunk_destroy(chunk);
    
    return err;
}

rtmp_error_t rtmp_send_video(rtmp_context_t *ctx, const uint8_t *data, size_t len, uint32_t timestamp) {
    if (!ctx || !data || len == 0) {
        return RTMP_ERROR_INVALID_STATE;
    }
    
    rtmp_chunk_t *chunk = rtmp_chunk_create();
    if (!chunk) {
        return RTMP_ERROR_MEMORY;
    }
    
    // Set chunk header for video
    rtmp_chunk_set_type(chunk, RTMP_CHUNK_TYPE_0);
    rtmp_chunk_set_message_type(chunk, RTMP_MSG_VIDEO);
    rtmp_chunk_set_message_length(chunk, len);
    rtmp_chunk_set_stream_id(chunk, 1);  // Default video stream ID
    rtmp_chunk_set_timestamp(chunk, timestamp);
    
    // Set video data
    if (rtmp_chunk_append_data(chunk, data, len) != RTMP_CHUNK_OK) {
        rtmp_chunk_destroy(chunk);
        return RTMP_ERROR_CHUNK;
    }
    
    // Send chunk
    rtmp_error_t err = rtmp_send_chunk(ctx, chunk);
    rtmp_chunk_destroy(chunk);
    
    return err;
}

rtmp_error_t rtmp_send_audio(rtmp_context_t *ctx, const uint8_t *data, size_t len, uint32_t timestamp) {
    if (!ctx || !data || len == 0) {
        return RTMP_ERROR_INVALID_STATE;
    }
    
    rtmp_chunk_t *chunk = rtmp_chunk_create();
    if (!chunk) {
        return RTMP_ERROR_MEMORY;
    }
    
    // Set chunk header for audio
    rtmp_chunk_set_type(chunk, RTMP_CHUNK_TYPE_0);
    rtmp_chunk_set_message_type(chunk, RTMP_MSG_AUDIO);
    rtmp_chunk_set_message_length(chunk, len);
    rtmp_chunk_set_stream_id(chunk, 1);  // Default audio stream ID
    rtmp_chunk_set_timestamp(chunk, timestamp);
    
    // Set audio data
    if (rtmp_chunk_append_data(chunk, data, len) != RTMP_CHUNK_OK) {
        rtmp_chunk_destroy(chunk);
        return RTMP_ERROR_CHUNK;
    }
    
    // Send chunk
    rtmp_error_t err = rtmp_send_chunk(ctx, chunk);
    rtmp_chunk_destroy(chunk);
    
    return err;
}

static rtmp_error_t rtmp_socket_connect(rtmp_context_t *ctx, const char *host, uint16_t port) {
    struct sockaddr_in addr;
    int sock;
    
    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        rtmp_set_error(ctx, RTMP_ERROR_SOCKET, "Failed to create socket: %s", strerror(errno));
        return RTMP_ERROR_SOCKET;
    }
    
    // Set socket options
    if (ctx->config.tcp_nodelay) {
        int flag = 1;
        setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    }
    
    // Set non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    // Connect
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(host);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if (errno != EINPROGRESS) {
            close(sock);
            rtmp_set_error(ctx, RTMP_ERROR_SOCKET, "Connect failed: %s", strerror(errno));
            return RTMP_ERROR_SOCKET;
        }
    }
    
    ctx->socket = sock;
    return RTMP_ERROR_OK;
}

static rtmp_error_t rtmp_socket_send(rtmp_context_t *ctx, const uint8_t *data, size_t len) {
    if (!ctx || ctx->socket < 0) {
        return RTMP_ERROR_SOCKET;
    }
    
    size_t sent = 0;
    while (sent < len) {
        ssize_t ret = send(ctx->socket, data + sent, len - sent, 0);
        if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block, try again later
                continue;
            }
            rtmp_set_error(ctx, RTMP_ERROR_SOCKET, "Send failed: %s", strerror(errno));
            return RTMP_ERROR_SOCKET;
        }
        sent += ret;
        ctx->stats.bytes_out += ret;
    }
    
    return RTMP_ERROR_OK;
}

static rtmp_error_t rtmp_socket_recv(rtmp_context_t *ctx, uint8_t *data, size_t len) {
    if (!ctx || ctx->socket < 0) {
        return RTMP_ERROR_SOCKET;
    }
    
    size_t received = 0;
    while (received < len) {
        ssize_t ret = recv(ctx->socket, data + received, len - received, 0);
        if (ret < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block, try again later
                continue;
            }
            rtmp_set_error(ctx, RTMP_ERROR_SOCKET, "Receive failed: %s", strerror(errno));
            return RTMP_ERROR_SOCKET;
        }
        if (ret == 0) {
            // Connection closed
            rtmp_set_error(ctx, RTMP_ERROR_SOCKET, "Connection closed by peer");
            return RTMP_ERROR_SOCKET;
        }
        received += ret;
        ctx->stats.bytes_in += ret;
    }
    
    return RTMP_ERROR_OK;
}

static rtmp_error_t rtmp_process_handshake(rtmp_context_t *ctx) {
    // Receive S0 (version)
    uint8_t version;
    if (rtmp_socket_recv(ctx, &version, 1) != RTMP_ERROR_OK) {
        return RTMP_ERROR_HANDSHAKE;
    }
    
    if (version != RTMP_VERSION) {
        rtmp_set_error(ctx, RTMP_ERROR_HANDSHAKE, "Unsupported RTMP version: %d", version);
        return RTMP_ERROR_HANDSHAKE;
    }
    
    // Receive S1
    if (rtmp_socket_recv(ctx, ctx->handshake_in, RTMP_HANDSHAKE_SIZE) != RTMP_ERROR_OK) {
        return RTMP_ERROR_HANDSHAKE;
    }
    
    // Send C2 (echo S1)
    if (rtmp_socket_send(ctx, ctx->handshake_in, RTMP_HANDSHAKE_SIZE) != RTMP_ERROR_OK) {
        return RTMP_ERROR_HANDSHAKE;
    }
    
    // Receive S2
    if (rtmp_socket_recv(ctx, ctx->handshake_in, RTMP_HANDSHAKE_SIZE) != RTMP_ERROR_OK) {
        return RTMP_ERROR_HANDSHAKE;
    }
    
    // Verify S2 matches C1
    if (memcmp(ctx->handshake_in, ctx->handshake_out, RTMP_HANDSHAKE_SIZE) != 0) {
        rtmp_set_error(ctx, RTMP_ERROR_HANDSHAKE, "Handshake verification failed");
        return RTMP_ERROR_HANDSHAKE;
    }
    
    ctx->state = RTMP_STATE_HANDSHAKE_DONE;
    
    if (ctx->callbacks.on_state_change) {
        ctx->callbacks.on_state_change(ctx, RTMP_STATE_VERSION_SENT, RTMP_STATE_HANDSHAKE_DONE);
    }
    
    return RTMP_ERROR_OK;
}

static rtmp_error_t rtmp_process_chunk(rtmp_context_t *ctx, rtmp_chunk_t *chunk) {
    if (!chunk || !rtmp_chunk_is_valid(chunk)) {
        return RTMP_ERROR_CHUNK;
    }
    
    // Update statistics
    ctx->stats.chunks_in++;
    
    // Call chunk callback if registered
    if (ctx->callbacks.on_chunk_received) {
        ctx->callbacks.on_chunk_received(ctx, chunk);
    }
    
    // Process based on message type
    switch (chunk->header.message_type) {
        case RTMP_MSG_SET_CHUNK_SIZE:
            if (chunk->data_size >= 4) {
                uint32_t new_size;
                memcpy(&new_size, chunk->data, 4);
                new_size = ntohl(new_size);
                if (new_size > 0) {
                    ctx->chunk_size = new_size;
                }
            }
            break;
            
        case RTMP_MSG_ABORT:
            // Handle abort message
            break;
            
        case RTMP_MSG_ACKNOWLEDGEMENT:
            // Handle acknowledgement
            break;
            
        case RTMP_MSG_WINDOW_ACK_SIZE:
            if (chunk->data_size >= 4) {
                uint32_t window_size;
                memcpy(&window_size, chunk->data, 4);
                ctx->config.window_size = ntohl(window_size);
            }
            break;
            
        default:
            // Call message callback for other types
            if (ctx->callbacks.on_message_received) {
                ctx->callbacks.on_message_received(ctx, 
                                                chunk->header.message_type,
                                                chunk->header.stream_id,
                                                chunk->data,
                                                chunk->data_size);
            }
            break;
    }
    
    return RTMP_ERROR_OK;
}

static rtmp_error_t rtmp_send_chunk(rtmp_context_t *ctx, rtmp_chunk_t *chunk) {
    if (!ctx || !chunk || !rtmp_chunk_is_valid(chunk)) {
        return RTMP_ERROR_CHUNK;
    }
    
    size_t header_size = rtmp_chunk_get_header_size(chunk->header.type);
    uint8_t header[16];  // Max header size
    size_t pos = 0;
    
    // Basic header (1-3 bytes)
    if (chunk->header.stream_id <= 63) {
        header[pos++] = (chunk->header.type << 6) | chunk->header.stream_id;
    } else if (chunk->header.stream_id <= 319) {
        header[pos++] = (chunk->header.type << 6) | 0;
        header[pos++] = chunk->header.stream_id - 64;
    } else {
        header[pos++] = (chunk->header.type << 6) | 1;
        uint16_t id = htons(chunk->header.stream_id - 64);
        memcpy(header + pos, &id, 2);
        pos += 2;
    }
    
    // Message header (0, 3, 7, or 11 bytes)
    if (chunk->header.type <= RTMP_CHUNK_TYPE_2) {
        // Timestamp
        uint32_t timestamp = htonl(chunk->header.timestamp);
        memcpy(header + pos, &timestamp, 3);
        pos += 3;
        
        if (chunk->header.type <= RTMP_CHUNK_TYPE_1) {
            // Message length and type
            uint32_t length = htonl(chunk->header.message_length);
            memcpy(header + pos, &length, 3);
            pos += 3;
            header[pos++] = chunk->header.message_type;
            
            if (chunk->header.type == RTMP_CHUNK_TYPE_0) {
                // Message stream ID
                uint32_t stream_id = htonl(chunk->header.stream_id);
                memcpy(header + pos, &stream_id, 4);
                pos += 4;
            }
        }
    }
    
    // Send header
    if (rtmp_socket_send(ctx, header, pos) != RTMP_ERROR_OK) {
        return RTMP_ERROR_SOCKET;
    }
    
    // Send data in chunks
    size_t remaining = chunk->data_size;
    size_t offset = 0;
    
    while (remaining > 0) {
        size_t chunk_size = remaining > ctx->chunk_size ? ctx->chunk_size : remaining;
        if (rtmp_socket_send(ctx, chunk->data + offset, chunk_size) != RTMP_ERROR_OK) {
            return RTMP_ERROR_SOCKET;
        }
        
        remaining -= chunk_size;
        offset += chunk_size;
        
        // Send continuation header if needed
        if (remaining > 0) {
            uint8_t continuation = (RTMP_CHUNK_TYPE_3 << 6) | (chunk->header.stream_id & 0x3F);
            if (rtmp_socket_send(ctx, &continuation, 1) != RTMP_ERROR_OK) {
                return RTMP_ERROR_SOCKET;
            }
        }
    }
    
    ctx->stats.chunks_out++;
    return RTMP_ERROR_OK;
}

static void rtmp_set_error(rtmp_context_t *ctx, rtmp_error_t error, const char *fmt, ...) {
    if (!ctx) return;
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(ctx->error_message, sizeof(ctx->error_message), fmt, args);
    va_end(args);
    
    if (ctx->callbacks.on_error) {
        ctx->callbacks.on_error(ctx, error, ctx->error_message);
    }
}

const char* rtmp_get_error_string(rtmp_error_t error) {
    switch (error) {
        case RTMP_ERROR_OK:
            return "Success";
        case RTMP_ERROR_INVALID_STATE:
            return "Invalid state";
        case RTMP_ERROR_SOCKET:
            return "Socket error";
        case RTMP_ERROR_HANDSHAKE:
            return "Handshake error";
        case RTMP_ERROR_CONNECT:
            return "Connect error";
        case RTMP_ERROR_STREAM:
            return "Stream error";
        case RTMP_ERROR_CHUNK:
            return "Chunk error";
        case RTMP_ERROR_PROTOCOL:
            return "Protocol error";
        case RTMP_ERROR_MEMORY:
            return "Memory error";
        case RTMP_ERROR_TIMEOUT:
            return "Timeout error";
        default:
            return "Unknown error";
    }
}

bool rtmp_is_valid_message_type(uint8_t type) {
    switch (type) {
        case RTMP_MSG_SET_CHUNK_SIZE:
        case RTMP_MSG_ABORT:
        case RTMP_MSG_ACKNOWLEDGEMENT:
        case RTMP_MSG_USER_CONTROL:
        case RTMP_MSG_WINDOW_ACK_SIZE:
        case RTMP_MSG_SET_PEER_BW:
        case RTMP_MSG_AUDIO:
        case RTMP_MSG_VIDEO:
        case RTMP_MSG_DATA_AMF3:
        case RTMP_MSG_SHARED_OBJ_AMF3:
        case RTMP_MSG_COMMAND_AMF3:
        case RTMP_MSG_DATA_AMF0:
        case RTMP_MSG_SHARED_OBJ_AMF0:
        case RTMP_MSG_COMMAND_AMF0:
        case RTMP_MSG_AGGREGATE:
            return true;
        default:
            return false;
    }
}

const char* rtmp_get_message_type_string(uint8_t type) {
    switch (type) {
        case RTMP_MSG_SET_CHUNK_SIZE:
            return "Set Chunk Size";
        case RTMP_MSG_ABORT:
            return "Abort";
        case RTMP_MSG_ACKNOWLEDGEMENT:
            return "Acknowledgement";
        case RTMP_MSG_USER_CONTROL:
            return "User Control";
        case RTMP_MSG_WINDOW_ACK_SIZE:
            return "Window Acknowledgement Size";
        case RTMP_MSG_SET_PEER_BW:
            return "Set Peer Bandwidth";
        case RTMP_MSG_AUDIO:
            return "Audio";
        case RTMP_MSG_VIDEO:
            return "Video";
        case RTMP_MSG_DATA_AMF3:
            return "Data (AMF3)";
        case RTMP_MSG_SHARED_OBJ_AMF3:
            return "Shared Object (AMF3)";
        case RTMP_MSG_COMMAND_AMF3:
            return "Command (AMF3)";
        case RTMP_MSG_DATA_AMF0:
            return "Data (AMF0)";
        case RTMP_MSG_SHARED_OBJ_AMF0:
            return "Shared Object (AMF0)";
        case RTMP_MSG_COMMAND_AMF0:
            return "Command (AMF0)";
        case RTMP_MSG_AGGREGATE:
            return "Aggregate";
        default:
            return "Unknown";
    }
}