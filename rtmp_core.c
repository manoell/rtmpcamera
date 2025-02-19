#include "rtmp_core.h"
#include "rtmp_log.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>

#define RTMP_DEFAULT_CHUNK_SIZE 128
#define RTMP_MAX_CHUNK_SIZE 16777215
#define RTMP_BUFFER_SIZE (16 * 1024)  // 16KB buffer

struct RTMPContext {
    int socket;
    uint32_t chunk_size;
    uint32_t window_size;
    uint32_t bytes_received;
    uint32_t bytes_sent;
    
    RTMPVideoCallback video_callback;
    RTMPAudioCallback audio_callback;
    void* video_userdata;
    void* audio_userdata;
    
    uint8_t* buffer;
    size_t buffer_size;
    size_t buffer_used;
    
    pthread_mutex_t mutex;
    int running;
};

static int server_socket = -1;
static int server_running = 0;
static pthread_t server_thread;
static pthread_mutex_t server_mutex = PTHREAD_MUTEX_INITIALIZER;

RTMPContext* rtmp_context_create(void) {
    RTMPContext* ctx = calloc(1, sizeof(RTMPContext));
    if (!ctx) {
        LOG_ERROR("Failed to allocate RTMP context");
        return NULL;
    }
    
    ctx->chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
    ctx->window_size = 2500000;
    ctx->buffer_size = RTMP_BUFFER_SIZE;
    ctx->buffer = malloc(ctx->buffer_size);
    
    if (!ctx->buffer) {
        LOG_ERROR("Failed to allocate buffer");
        free(ctx);
        return NULL;
    }
    
    pthread_mutex_init(&ctx->mutex, NULL);
    ctx->running = 1;
    
    LOG_DEBUG("RTMP context created");
    return ctx;
}

void rtmp_context_destroy(RTMPContext* ctx) {
    if (!ctx) return;
    
    ctx->running = 0;
    pthread_mutex_destroy(&ctx->mutex);
    free(ctx->buffer);
    free(ctx);
    
    LOG_DEBUG("RTMP context destroyed");
}

void rtmp_set_video_callback(RTMPContext* ctx, RTMPVideoCallback cb, void* userdata) {
    if (!ctx) return;
    pthread_mutex_lock(&ctx->mutex);
    ctx->video_callback = cb;
    ctx->video_userdata = userdata;
    pthread_mutex_unlock(&ctx->mutex);
}

void rtmp_set_audio_callback(RTMPContext* ctx, RTMPAudioCallback cb, void* userdata) {
    if (!ctx) return;
    pthread_mutex_lock(&ctx->mutex);
    ctx->audio_callback = cb;
    ctx->audio_userdata = userdata;
    pthread_mutex_unlock(&ctx->mutex);
}

static int process_video_packet(RTMPContext* ctx, const uint8_t* data, size_t len, uint32_t timestamp) {
    if (ctx->video_callback) {
        return ctx->video_callback(ctx->video_userdata, data, len, timestamp);
    }
    return 0;
}

static int process_audio_packet(RTMPContext* ctx, const uint8_t* data, size_t len, uint32_t timestamp) {
    if (ctx->audio_callback) {
        return ctx->audio_callback(ctx->audio_userdata, data, len, timestamp);
    }
    return 0;
}

int rtmp_process_packet(RTMPContext* ctx, const uint8_t* data, size_t len) {
    if (!ctx || !data || !len) return -1;
    
    pthread_mutex_lock(&ctx->mutex);
    
    // Copy data to internal buffer if needed
    if (ctx->buffer_used + len > ctx->buffer_size) {
        size_t new_size = ctx->buffer_size * 2;
        while (new_size < ctx->buffer_used + len) new_size *= 2;
        
        uint8_t* new_buffer = realloc(ctx->buffer, new_size);
        if (!new_buffer) {
            LOG_ERROR("Failed to resize buffer to %zu bytes", new_size);
            pthread_mutex_unlock(&ctx->mutex);
            return -1;
        }
        
        ctx->buffer = new_buffer;
        ctx->buffer_size = new_size;
    }
    
    memcpy(ctx->buffer + ctx->buffer_used, data, len);
    ctx->buffer_used += len;
    
    // Process complete packets
    while (ctx->buffer_used >= 1) {
        uint8_t header = ctx->buffer[0];
        uint8_t chunk_type = header >> 6;
        
        size_t header_size;
        switch (chunk_type) {
            case RTMP_CHUNK_TYPE_0: header_size = 11; break;
            case RTMP_CHUNK_TYPE_1: header_size = 7; break;
            case RTMP_CHUNK_TYPE_2: header_size = 3; break;
            case RTMP_CHUNK_TYPE_3: header_size = 0; break;
            default:
                LOG_ERROR("Invalid chunk type: %d", chunk_type);
                pthread_mutex_unlock(&ctx->mutex);
                return -1;
        }
        
        if (ctx->buffer_used < header_size + 1) break;
        
        // Parse message header
        uint32_t timestamp = 0;
        uint32_t message_length = 0;
        uint8_t message_type = 0;
        
        size_t pos = 1;
        
        if (chunk_type == RTMP_CHUNK_TYPE_0) {
            timestamp = (ctx->buffer[pos] << 16) | (ctx->buffer[pos+1] << 8) | ctx->buffer[pos+2];
            pos += 3;
            message_length = (ctx->buffer[pos] << 16) | (ctx->buffer[pos+1] << 8) | ctx->buffer[pos+2];
            pos += 3;
            message_type = ctx->buffer[pos++];
            pos += 4; // Skip message stream id
        }
        
        // Check if we have the full message
        if (ctx->buffer_used < pos + message_length) break;
        
        // Process message based on type
        switch (message_type) {
            case RTMP_MSG_VIDEO:
                process_video_packet(ctx, ctx->buffer + pos, message_length, timestamp);
                break;
                
            case RTMP_MSG_AUDIO:
                process_audio_packet(ctx, ctx->buffer + pos, message_length, timestamp);
                break;
                
            default:
                LOG_DEBUG("Unhandled message type: %d", message_type);
                break;
        }
        
        // Remove processed data from buffer
        size_t total_length = pos + message_length;
        memmove(ctx->buffer, ctx->buffer + total_length, ctx->buffer_used - total_length);
        ctx->buffer_used -= total_length;
    }
    
    pthread_mutex_unlock(&ctx->mutex);
    return 0;
}

static void* server_thread_func(void* arg) {
    int port = *(int*)arg;
    free(arg);
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        LOG_ERROR("Failed to create server socket");
        return NULL;
    }
    
    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind server socket");
        close(server_socket);
        return NULL;
    }
    
    if (listen(server_socket, 5) < 0) {
        LOG_ERROR("Failed to listen on server socket");
        close(server_socket);
        return NULL;
    }
    
    LOG_INFO("RTMP server listening on port %d", port);
    
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (server_running) {
                LOG_ERROR("Failed to accept client connection");
            }
            continue;
        }
        
        RTMPContext* ctx = rtmp_context_create();
        if (!ctx) {
            close(client_socket);
            continue;
        }
        
        ctx->socket = client_socket;
        // Handle client in new thread...
    }
    
    return NULL;
}

int rtmp_server_start(int port) {
    pthread_mutex_lock(&server_mutex);
    
    if (server_running) {
        pthread_mutex_unlock(&server_mutex);
        return -1;
    }
    
    server_running = 1;
    
    int* port_arg = malloc(sizeof(int));
    if (!port_arg) {
        pthread_mutex_unlock(&server_mutex);
        return -1;
    }
    
    *port_arg = port;
    
    if (pthread_create(&server_thread, NULL, server_thread_func, port_arg) != 0) {
        free(port_arg);
        server_running = 0;
        pthread_mutex_unlock(&server_mutex);
        return -1;
    }
    
    pthread_mutex_unlock(&server_mutex);
    return 0;
}

void rtmp_server_stop(void) {
    pthread_mutex_lock(&server_mutex);
    
    if (server_running) {
        server_running = 0;
        if (server_socket >= 0) {
            close(server_socket);
            server_socket = -1;
        }
        pthread_join(server_thread, NULL);
    }
    
    pthread_mutex_unlock(&server_mutex);
}