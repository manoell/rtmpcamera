// rtmp_core.c
#include "rtmp_core.h"
#include "rtmp_protocol.h"
#include "rtmp_handshake.h"
#include "rtmp_utils.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define RTMP_MAX_CLIENTS 10
#define RTMP_BUFFER_SIZE (1024 * 1024) // 1MB buffer
#define RTMP_LOG_FILE "/tmp/rtmpcamera.log"

typedef struct {
    int running;
    int server_socket;
    pthread_t accept_thread;
    RTMPSession* sessions[RTMP_MAX_CLIENTS];
    pthread_mutex_t sessions_mutex;
    RTMPConfig config;
    FILE* log_file;
    ConnectionCallback conn_callback;
    void* user_data;
    
    struct {
        uint32_t active_connections;
        uint32_t total_connections;
        uint32_t failed_connections;
        uint64_t bytes_received;
        uint64_t bytes_sent;
    } stats;
} RTMPServer;

static RTMPServer* g_server = NULL;

// Logging helper
static void rtmp_log(const char* format, ...) {
    if (!g_server || !g_server->log_file) return;
    
    va_list args;
    va_start(args, format);
    
    time_t now;
    time(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    fprintf(g_server->log_file, "[%s] ", timestamp);
    vfprintf(g_server->log_file, format, args);
    fprintf(g_server->log_file, "\n");
    fflush(g_server->log_file);
    
    va_end(args);
}

// Memory pool for optimized buffer allocation
static uint8_t* alloc_buffer(void) {
    return (uint8_t*)aligned_alloc(16, RTMP_BUFFER_SIZE);
}

static void free_buffer(uint8_t* buffer) {
    free(buffer);
}

// Session management
static int add_session(RTMPSession* session) {
    if (!g_server) return -1;
    
    pthread_mutex_lock(&g_server->sessions_mutex);
    
    int slot = -1;
    for (int i = 0; i < RTMP_MAX_CLIENTS; i++) {
        if (!g_server->sessions[i]) {
            g_server->sessions[i] = session;
            g_server->stats.active_connections++;
            g_server->stats.total_connections++;
            slot = i;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_server->sessions_mutex);
    return slot;
}

static void remove_session(RTMPSession* session) {
    if (!g_server) return;
    
    pthread_mutex_lock(&g_server->sessions_mutex);
    
    for (int i = 0; i < RTMP_MAX_CLIENTS; i++) {
        if (g_server->sessions[i] == session) {
            g_server->sessions[i] = NULL;
            g_server->stats.active_connections--;
            break;
        }
    }
    
    pthread_mutex_unlock(&g_server->sessions_mutex);
}

static void cleanup_session(RTMPSession* session) {
    if (!session) return;
    
    rtmp_log("Cleaning up session %p", session);
    
    if (session->socket >= 0) {
        shutdown(session->socket, SHUT_RDWR);
        close(session->socket);
    }
    
    remove_session(session);
    
    if (session->buffer) {
        free_buffer(session->buffer);
    }
    
    if (session->handshake_data) {
        free(session->handshake_data);
    }
    
    pthread_mutex_destroy(&session->mutex);
    free(session);
}

// Client handler thread
static void* client_handler(void* arg) {
    RTMPSession* session = (RTMPSession*)arg;
    
    // Set TCP keepalive
    int keepalive = 1;
    int keepidle = 60;
    int keepintvl = 10;
    int keepcnt = 3;
    
    setsockopt(session->socket, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    setsockopt(session->socket, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(session->socket, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    setsockopt(session->socket, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
    
    rtmp_log("New client connected from %s", session->ip_address);
    
    // Allocate session buffer
    session->buffer = alloc_buffer();
    if (!session->buffer) {
        rtmp_log("Failed to allocate session buffer");
        goto cleanup;
    }
    
    // Initialize session mutex
    pthread_mutex_init(&session->mutex, NULL);
    
    // Perform handshake
    if (rtmp_handshake_process(session) < 0) {
        rtmp_log("Handshake failed for session %p", session);
        g_server->stats.failed_connections++;
        goto cleanup;
    }
    
    rtmp_log("Handshake successful for session %p", session);
    
    // Main processing loop
    while (session->running && g_server->running) {
        // Set receive timeout
        struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
        setsockopt(session->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        ssize_t bytes = recv(session->socket, 
                           session->buffer + session->buffer_offset,
                           RTMP_BUFFER_SIZE - session->buffer_offset,
                           0);
        
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout - check connection status
                continue;
            }
            rtmp_log("Receive error on session %p: %s", session, strerror(errno));
            break;
        }
        
        if (bytes == 0) {
            rtmp_log("Client disconnected for session %p", session);
            break;
        }
        
        pthread_mutex_lock(&session->mutex);
        
        session->buffer_offset += bytes;
        g_server->stats.bytes_received += bytes;
        
        // Process RTMP chunks
        size_t processed = 0;
        while (processed < session->buffer_offset) {
            RTMPChunk chunk;
            int ret = rtmp_chunk_parse(session->buffer + processed,
                                     session->buffer_offset - processed,
                                     &chunk);
            
            if (ret < 0) break;
            
            processed += ret;
            
            // Handle RTMP message
            if (rtmp_protocol_handle_message(session, &chunk) < 0) {
                rtmp_log("Error processing message for session %p", session);
                goto cleanup_locked;
            }
        }
        
        // Move unprocessed data to start of buffer
        if (processed < session->buffer_offset) {
            memmove(session->buffer,
                    session->buffer + processed,
                    session->buffer_offset - processed);
        }
        session->buffer_offset -= processed;
        
        pthread_mutex_unlock(&session->mutex);
        continue;
        
    cleanup_locked:
        pthread_mutex_unlock(&session->mutex);
        break;
    }
    
cleanup:
    cleanup_session(session);
    return NULL;
}

// Accept handler thread
static void* accept_handler(void* arg) {
    RTMPServer* server = (RTMPServer*)arg;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    while (server->running) {
        int client_socket = accept(server->server_socket,
                                 (struct sockaddr*)&client_addr,
                                 &addr_len);
        
        if (client_socket < 0) {
            if (errno != EINTR) {
                rtmp_log("Accept error: %s", strerror(errno));
            }
            continue;
        }
        
        // Set socket options
        int flag = 1;
        setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        
        // Create new session
        RTMPSession* session = (RTMPSession*)calloc(1, sizeof(RTMPSession));
        if (!session) {
            close(client_socket);
            continue;
        }
        
        session->socket = client_socket;
        session->running = 1;
        
        // Store client IP
        inet_ntop(AF_INET, &client_addr.sin_addr,
                 session->ip_address, sizeof(session->ip_address));
        
        // Add to session list
        if (add_session(session) < 0) {
            rtmp_log("Max clients reached, rejecting connection from %s",
                    session->ip_address);
            cleanup_session(session);
            continue;
        }
        
        // Notify connection callback
        if (server->conn_callback) {
            server->conn_callback(session, 1, server->user_data);
        }
        
        // Start client thread
        pthread_t thread;
        if (pthread_create(&thread, NULL, client_handler, session) != 0) {
            rtmp_log("Failed to create client thread for %s", 
                    session->ip_address);
            cleanup_session(session);
            continue;
        }
        pthread_detach(thread);
    }
    
    return NULL;
}

// Public API
int rtmp_server_init(const RTMPConfig* config, ConnectionCallback cb, void* user_data) {
    if (g_server) return -1;
    
    g_server = (RTMPServer*)calloc(1, sizeof(RTMPServer));
    if (!g_server) return -1;
    
    // Open log file
    g_server->log_file = fopen(RTMP_LOG_FILE, "a");
    if (!g_server->log_file) {
        free(g_server);
        g_server = NULL;
        return -1;
    }
    
    // Copy config
    if (config) {
        memcpy(&g_server->config, config, sizeof(RTMPConfig));
    }
    
    g_server->conn_callback = cb;
    g_server->user_data = user_data;
    
    pthread_mutex_init(&g_server->sessions_mutex, NULL);
    
    rtmp_log("RTMP server initialized");
    return 0;
}

int rtmp_server_start(void) {
    if (!g_server) return -1;
    
    // Create server socket
    g_server->server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server->server_socket < 0) {
        rtmp_log("Failed to create server socket: %s", strerror(errno));
        return -1;
    }
    
    // Set socket options
    int reuse = 1;
    setsockopt(g_server->server_socket, SOL_SOCKET, SO_REUSEADDR, 
               &reuse, sizeof(reuse));
    
    // Bind address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(g_server->config.port ? 
                         g_server->config.port : 1935);
    
    if (bind(g_server->server_socket, (struct sockaddr*)&addr,
             sizeof(addr)) < 0) {
        rtmp_log("Failed to bind server socket: %s", strerror(errno));
        close(g_server->server_socket);
        return -1;
    }
    
    // Start listening
    if (listen(g_server->server_socket, 5) < 0) {
        rtmp_log("Failed to listen on server socket: %s", strerror(errno));
        close(g_server->server_socket);
        return -1;
    }
    
    g_server->running = 1;
    
    // Start accept thread
    if (pthread_create(&g_server->accept_thread, NULL,
                      accept_handler, g_server) != 0) {
        rtmp_log("Failed to create accept thread: %s", strerror(errno));
        close(g_server->server_socket);
        g_server->running = 0;
        return -1;
    }
    
    rtmp_log("RTMP server started on port %d",
             g_server->config.port ? g_server->config.port : 1935);
    return 0;
}

void rtmp_server_stop(void) {
    if (!g_server) return;
    
    rtmp_log("Stopping RTMP server");
    
    g_server->running = 0;
    
    // Close server socket
    if (g_server->server_socket >= 0) {
        shutdown(g_server->server_socket, SHUT_RDWR);
        close(g_server->server_socket);
    }
    
    // Wait for accept thread
    pthread_join(g_server->accept_thread, NULL);
    
    // Cleanup all sessions
    pthread_mutex_lock(&g_server->sessions_mutex);
    for (int i = 0; i < RTMP_MAX_CLIENTS; i++) {
        if (g_server->sessions[i]) {
            cleanup_session(g_server->sessions[i]);
        }
    }
    pthread_mutex_unlock(&g_server->sessions_mutex);
    
    pthread_mutex_destroy(&g_server->sessions_mutex);
    
    if (g_server->log_file) {
        fclose(g_server->log_file);
    }
    
    free(g_server);
    g_server = NULL;
    
    rtmp_log("RTMP server stopped");
}

void rtmp_server_get_stats(RTMPServerStats* stats) {
    if (!g_server || !stats) return;
    
    stats->active_connections = g_server->stats.active_connections;
    stats->total_connections = g_server->stats.total_connections;
    stats->failed_connections = g_server->stats.failed_connections;
    stats->bytes_received = g_server->stats.bytes_received;
    stats->bytes_sent = g_server->stats.bytes_sent;
}