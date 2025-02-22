#include "rtmp_core.h"
#include "rtmp_chunk.h"
#include "rtmp_protocol.h"
#include "rtmp_quality.h"
#include "rtmp_diagnostics.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define RTMP_DEFAULT_PORT 1935
#define RTMP_BUFFER_SIZE 65536
#define RTMP_MAX_STREAMS 8
#define RTMP_PING_INTERVAL 5000 // 5 seconds

struct RTMPContext {
    int socket;
    bool connected;
    bool running;
    
    // Connection info
    char hostname[256];
    int port;
    char app_name[128];
    char stream_name[128];
    
    // Buffers
    uint8_t *in_buffer;
    uint8_t *out_buffer;
    size_t in_buffer_size;
    size_t out_buffer_size;
    
    // Stream management
    RTMPStream *streams[RTMP_MAX_STREAMS];
    uint32_t stream_count;
    uint32_t current_stream_id;
    
    // Protocol state
    uint32_t chunk_size;
    uint32_t window_size;
    uint32_t bandwidth;
    
    // Threading
    pthread_t network_thread;
    pthread_mutex_t mutex;
    
    // Callbacks
    rtmp_core_callback_t callback;
    void *callback_context;
    
    // Stats and monitoring
    struct {
        uint64_t bytes_sent;
        uint64_t bytes_received;
        uint32_t messages_sent;
        uint32_t messages_received;
        struct timeval last_ping;
    } stats;
};

static void rtmp_handle_error(RTMPContext *ctx, const char *message) {
    if (ctx && ctx->callback) {
        RTMPCoreEvent event = {
            .type = RTMP_CORE_EVENT_ERROR,
            .error_message = message
        };
        ctx->callback(&event, ctx->callback_context);
    }
    rtmp_diagnostic_log("Error: %s", message);
}

static bool rtmp_send_packet(RTMPContext *ctx, RTMPPacket *packet) {
    if (!ctx || !packet) return false;
    
    pthread_mutex_lock(&ctx->mutex);
    
    // Prepare chunk
    size_t chunk_size = rtmp_chunk_get_size(packet);
    if (chunk_size > ctx->out_buffer_size) {
        pthread_mutex_unlock(&ctx->mutex);
        rtmp_handle_error(ctx, "Packet too large for output buffer");
        return false;
    }
    
    // Serialize packet to chunk
    size_t written = rtmp_chunk_serialize(packet, ctx->out_buffer, ctx->out_buffer_size);
    if (written == 0) {
        pthread_mutex_unlock(&ctx->mutex);
        rtmp_handle_error(ctx, "Failed to serialize packet");
        return false;
    }
    
    // Send chunk
    ssize_t sent = send(ctx->socket, ctx->out_buffer, written, 0);
    if (sent < 0) {
        pthread_mutex_unlock(&ctx->mutex);
        rtmp_handle_error(ctx, "Send failed");
        return false;
    }
    
    // Update stats
    ctx->stats.bytes_sent += sent;
    ctx->stats.messages_sent++;
    
    pthread_mutex_unlock(&ctx->mutex);
    return true;
}

static RTMPPacket* rtmp_receive_packet(RTMPContext *ctx) {
    if (!ctx) return NULL;
    
    pthread_mutex_lock(&ctx->mutex);
    
    // Read chunk header
    uint8_t header[RTMP_MAX_HEADER_SIZE];
    ssize_t received = recv(ctx->socket, header, RTMP_MAX_HEADER_SIZE, MSG_PEEK);
    if (received <= 0) {
        pthread_mutex_unlock(&ctx->mutex);
        return NULL;
    }
    
    // Parse chunk size
    size_t chunk_size = rtmp_chunk_get_header_size(header);
    if (chunk_size > ctx->in_buffer_size) {
        pthread_mutex_unlock(&ctx->mutex);
        rtmp_handle_error(ctx, "Incoming chunk too large");
        return NULL;
    }
    
    // Read full chunk
    received = recv(ctx->socket, ctx->in_buffer, chunk_size, 0);
    if (received != chunk_size) {
        pthread_mutex_unlock(&ctx->mutex);
        rtmp_handle_error(ctx, "Failed to receive complete chunk");
        return NULL;
    }
    
    // Update stats
    ctx->stats.bytes_received += received;
    ctx->stats.messages_received++;
    
    // Parse packet
    RTMPPacket *packet = rtmp_chunk_parse(ctx->in_buffer, received);
    
    pthread_mutex_unlock(&ctx->mutex);
    return packet;
}

static void* rtmp_network_thread(void *arg) {
    RTMPContext *ctx = (RTMPContext*)arg;
    struct timeval now;
    
    while (ctx->running) {
        RTMPPacket *packet = rtmp_receive_packet(ctx);
        if (packet) {
            // Handle packet based on type
            switch (packet->m_packetType) {
                case RTMP_PACKET_TYPE_CHUNK_SIZE:
                    ctx->chunk_size = rtmp_protocol_get_chunk_size(packet);
                    break;
                    
                case RTMP_PACKET_TYPE_BYTES_READ:
                    ctx->window_size = rtmp_protocol_get_bytes_read(packet);
                    break;
                    
                case RTMP_PACKET_TYPE_BANDWIDTH:
                    ctx->bandwidth = rtmp_protocol_get_bandwidth(packet);
                    break;
                    
                case RTMP_PACKET_TYPE_VIDEO:
                case RTMP_PACKET_TYPE_AUDIO:
                    if (ctx->current_stream_id < ctx->stream_count) {
                        RTMPStream *stream = ctx->streams[ctx->current_stream_id];
                        rtmp_quality_update(stream, packet);
                    }
                    break;
                    
                default:
                    // Handle other packet types through protocol layer
                    rtmp_protocol_handle_packet(packet);
                    break;
            }
            
            // Notify callback
            if (ctx->callback) {
                RTMPCoreEvent event = {
                    .type = RTMP_CORE_EVENT_PACKET,
                    .packet = packet
                };
                ctx->callback(&event, ctx->callback_context);
            }
            
            rtmp_packet_free(packet);
        }
        
        // Handle ping/keepalive
        gettimeofday(&now, NULL);
        if ((now.tv_sec - ctx->stats.last_ping.tv_sec) * 1000 + 
            (now.tv_usec - ctx->stats.last_ping.tv_usec) / 1000 >= RTMP_PING_INTERVAL) {
            RTMPPacket ping_packet;
            rtmp_protocol_create_ping(&ping_packet);
            rtmp_send_packet(ctx, &ping_packet);
            ctx->stats.last_ping = now;
        }
        
        // Small sleep to prevent CPU hogging
        usleep(1000);
    }
    
    return NULL;
}

RTMPContext* rtmp_core_create(void) {
    RTMPContext *ctx = (RTMPContext*)calloc(1, sizeof(RTMPContext));
    if (!ctx) return NULL;
    
    ctx->in_buffer = (uint8_t*)malloc(RTMP_BUFFER_SIZE);
    ctx->out_buffer = (uint8_t*)malloc(RTMP_BUFFER_SIZE);
    ctx->in_buffer_size = RTMP_BUFFER_SIZE;
    ctx->out_buffer_size = RTMP_BUFFER_SIZE;
    
    pthread_mutex_init(&ctx->mutex, NULL);
    
    ctx->chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
    ctx->port = RTMP_DEFAULT_PORT;
    
    gettimeofday(&ctx->stats.last_ping, NULL);
    
    return ctx;
}

void rtmp_core_destroy(RTMPContext *ctx) {
    if (!ctx) return;
    
    rtmp_core_disconnect(ctx);
    
    pthread_mutex_destroy(&ctx->mutex);
    
    free(ctx->in_buffer);
    free(ctx->out_buffer);
    free(ctx);
}

bool rtmp_core_connect(RTMPContext *ctx, const char *url) {
    if (!ctx || !url) return false;
    
    // Parse URL
    if (!rtmp_protocol_parse_url(url, ctx->hostname, sizeof(ctx->hostname),
                                &ctx->port, ctx->app_name, sizeof(ctx->app_name),
                                ctx->stream_name, sizeof(ctx->stream_name))) {
        rtmp_handle_error(ctx, "Invalid RTMP URL");
        return false;
    }
    
    // Create socket
    ctx->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->socket < 0) {
        rtmp_handle_error(ctx, "Failed to create socket");
        return false;
    }
    
    // Set socket options
    int flag = 1;
    setsockopt(ctx->socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    // Connect
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ctx->port);
    if (inet_pton(AF_INET, ctx->hostname, &addr.sin_addr) <= 0) {
        rtmp_handle_error(ctx, "Invalid address");
        close(ctx->socket);
        return false;
    }
    
    if (connect(ctx->socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        rtmp_handle_error(ctx, "Connection failed");
        close(ctx->socket);
        return false;
    }
    
    // Set non-blocking
    int flags = fcntl(ctx->socket, F_GETFL, 0);
    fcntl(ctx->socket, F_SETFL, flags | O_NONBLOCK);
    
    // Start network thread
    ctx->running = true;
    if (pthread_create(&ctx->network_thread, NULL, rtmp_network_thread, ctx) != 0) {
        rtmp_handle_error(ctx, "Failed to create network thread");
        close(ctx->socket);
        ctx->running = false;
        return false;
    }
    
    ctx->connected = true;
    
    // Notify connection
    if (ctx->callback) {
        RTMPCoreEvent event = {
            .type = RTMP_CORE_EVENT_CONNECTED
        };
        ctx->callback(&event, ctx->callback_context);
    }
    
    return true;
}

void rtmp_core_disconnect(RTMPContext *ctx) {
    if (!ctx || !ctx->connected) return;
    
    ctx->running = false;
    pthread_join(ctx->network_thread, NULL);
    
    close(ctx->socket);
    ctx->socket = -1;
    ctx->connected = false;
    
    // Notify disconnection
    if (ctx->callback) {
        RTMPCoreEvent event = {
            .type = RTMP_CORE_EVENT_DISCONNECTED
        };
        ctx->callback(&event, ctx->callback_context);
    }
}

bool rtmp_core_add_stream(RTMPContext *ctx, RTMPStream *stream) {
    if (!ctx || !stream || ctx->stream_count >= RTMP_MAX_STREAMS) return false;
    
    pthread_mutex_lock(&ctx->mutex);
    ctx->streams[ctx->stream_count] = stream;
    ctx->stream_count++;
    pthread_mutex_unlock(&ctx->mutex);
    
    return true;
}

void rtmp_core_remove_stream(RTMPContext *ctx, RTMPStream *stream) {
    if (!ctx || !stream) return;
    
    pthread_mutex_lock(&ctx->mutex);
    for (uint32_t i = 0; i < ctx->stream_count; i++) {
        if (ctx->streams[i] == stream) {
            // Shift remaining streams
            for (uint32_t j = i; j < ctx->stream_count - 1; j++) {
                ctx->streams[j] = ctx->streams[j + 1];
            }
            ctx->stream_count--;
            break;
        }
    }
    pthread_mutex_unlock(&ctx->mutex);
}

void rtmp_core_set_callback(RTMPContext *ctx, rtmp_core_callback_t callback, void *context) {
    if (!ctx) return;
    
    pthread_mutex_lock(&ctx->mutex);
    ctx->callback = callback;
    ctx->callback_context = context;
    pthread_mutex_unlock(&ctx->mutex);
}

bool rtmp_core_is_connected(RTMPContext *ctx) {
    return ctx ? ctx->connected : false;
}

void rtmp_core_get_stats(RTMPContext *ctx, RTMPCoreStats *stats) {
    if (!ctx || !stats) return;
    
    pthread_mutex_lock(&ctx->mutex);
    stats->bytes_sent = ctx->stats.bytes_sent;
    stats->bytes_received = ctx->stats.bytes_received;
    stats->messages_sent = ctx->stats.messages_sent;
    stats->messages_received = ctx->stats.messages_received;
    pthread_mutex_unlock(&ctx->mutex);
}