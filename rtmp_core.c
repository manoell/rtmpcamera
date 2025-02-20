#include "rtmp_core.h"
#include "rtmp_log.h"
#include "rtmp_util.h"
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define RTMP_DEFAULT_CHUNK_SIZE 128
#define RTMP_MAX_CHUNK_SIZE 16777215
#define RTMP_BUFFER_SIZE (16 * 1024)  // 16KB buffer
#define MAX_CLIENTS 10

typedef struct {
    int socket;
    struct sockaddr_in addr;
    RTMPContext* ctx;
    pthread_t thread;
    int running;
} RTMPClient;

typedef struct {
    int server_socket;
    RTMPClient* clients[MAX_CLIENTS];
    pthread_mutex_t clients_mutex;
    pthread_t accept_thread;
    int running;
} RTMPServer;

static RTMPServer* server = NULL;

static int set_nonblocking(int socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(socket, F_SETFL, flags | O_NONBLOCK);
}

static int setup_tcp_socket(int socket) {
    int opt = 1;
    
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("Failed to set SO_REUSEADDR");
        return -1;
    }
    
    if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("Failed to set TCP_NODELAY");
        return -1;
    }
    
    if (setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &opt, sizeof(opt)) < 0) {
        LOG_ERROR("Failed to set SO_KEEPALIVE");
        return -1;
    }
    
    int rcvbuf = 256 * 1024;
    int sndbuf = 256 * 1024;
    
    if (setsockopt(socket, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
        LOG_ERROR("Failed to set SO_RCVBUF");
        return -1;
    }
    
    if (setsockopt(socket, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) < 0) {
        LOG_ERROR("Failed to set SO_SNDBUF");
        return -1;
    }
    
    return 0;
}

static void cleanup_client(RTMPClient* client) {
    if (!client) return;
    
    LOG_INFO("Cleaning up client connection");
    
    client->running = 0;
    
    if (client->socket >= 0) {
        close(client->socket);
        client->socket = -1;
    }
    
    if (client->ctx) {
        rtmp_context_destroy(client->ctx);
        client->ctx = NULL;
    }
    
    free(client);
}

static void* handle_client(void* arg) {
    RTMPClient* client = (RTMPClient*)arg;
    uint8_t buffer[RTMP_BUFFER_SIZE];
    char client_ip[INET_ADDRSTRLEN];
    
    inet_ntop(AF_INET, &client->addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client->addr.sin_port);
    
    LOG_INFO("New client connected from %s:%d", client_ip, client_port);
    
    while (client->running) {
        ssize_t bytes = recv(client->socket, buffer, sizeof(buffer), 0);
        if (bytes > 0) {
            if (rtmp_process_packet(client->ctx, buffer, bytes) < 0) {
                LOG_ERROR("Failed to process packet from %s:%d", client_ip, client_port);
                break;
            }
        } 
        else if (bytes == 0) {
            LOG_INFO("Client %s:%d disconnected", client_ip, client_port);
            break;
        } 
        else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG_ERROR("Error receiving from %s:%d: %s", 
                         client_ip, client_port, strerror(errno));
                break;
            }
            usleep(1000); // Evitar CPU alto quando não há dados
        }
    }
    
    // Remover cliente da lista
    pthread_mutex_lock(&server->clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i] == client) {
            server->clients[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&server->clients_mutex);
    
    cleanup_client(client);
    return NULL;
}

static void* accept_thread(void* arg) {
    while (server && server->running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_sock = accept(server->server_socket, 
                               (struct sockaddr*)&client_addr, 
                               &addr_len);
        
        if (client_sock < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG_ERROR("Accept failed: %s", strerror(errno));
            }
            usleep(1000); // Evitar CPU alto quando não há conexões
            continue;
        }
        
        // Configurar socket
        if (set_nonblocking(client_sock) < 0 || setup_tcp_socket(client_sock) < 0) {
            LOG_ERROR("Failed to configure client socket");
            close(client_sock);
            continue;
        }
        
        // Procurar slot livre
        pthread_mutex_lock(&server->clients_mutex);
        int slot = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (server->clients[i] == NULL) {
                slot = i;
                break;
            }
        }
        pthread_mutex_unlock(&server->clients_mutex);
        
        if (slot < 0) {
            LOG_ERROR("Maximum number of clients reached");
            close(client_sock);
            continue;
        }
        
        // Criar estrutura do cliente
        RTMPClient* client = calloc(1, sizeof(RTMPClient));
        if (!client) {
            LOG_ERROR("Failed to allocate client structure");
            close(client_sock);
            continue;
        }
        
        client->socket = client_sock;
        client->addr = client_addr;
        client->running = 1;
        client->ctx = rtmp_context_create();
        
        if (!client->ctx) {
            LOG_ERROR("Failed to create RTMP context");
            cleanup_client(client);
            continue;
        }
        
        // Criar thread para o cliente
        if (pthread_create(&client->thread, NULL, handle_client, client) != 0) {
            LOG_ERROR("Failed to create client thread");
            cleanup_client(client);
            continue;
        }
        
        pthread_detach(client->thread);
        
        // Adicionar à lista
        pthread_mutex_lock(&server->clients_mutex);
        server->clients[slot] = client;
        pthread_mutex_unlock(&server->clients_mutex);
    }
    
    return NULL;
}

RTMPContext* rtmp_context_create(void) {
    RTMPContext* ctx = calloc(1, sizeof(RTMPContext));
    if (!ctx) {
        LOG_ERROR("Failed to allocate RTMP context");
        return NULL;
    }
    
    ctx->chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
    ctx->buffer = buffer_alloc(RTMP_BUFFER_SIZE);
    if (!ctx->buffer) {
        free(ctx);
        return NULL;
    }
    
    ctx->buffer_size = RTMP_BUFFER_SIZE;
    ctx->state = RTMP_STATE_INIT;
    
    return ctx;
}

void rtmp_context_destroy(RTMPContext* ctx) {
    if (!ctx) return;
    buffer_free(ctx->buffer);
    free(ctx);
}

int rtmp_server_start(int port) {
    if (server) {
        LOG_ERROR("RTMP server already running");
        return -1;
    }
    
    server = calloc(1, sizeof(RTMPServer));
    if (!server) {
        LOG_ERROR("Failed to allocate server structure");
        return -1;
    }
    
    // Criar socket
    server->server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_socket < 0) {
        LOG_ERROR("Failed to create server socket: %s", strerror(errno));
        free(server);
        server = NULL;
        return -1;
    }
    
    // Configurar socket
    if (set_nonblocking(server->server_socket) < 0 || 
        setup_tcp_socket(server->server_socket) < 0) {
        LOG_ERROR("Failed to configure server socket");
        close(server->server_socket);
        free(server);
        server = NULL;
        return -1;
    }
    
    // Bind
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(server->server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind: %s", strerror(errno));
        close(server->server_socket);
        free(server);
        server = NULL;
        return -1;
    }
    
    // Listen
    if (listen(server->server_socket, MAX_CLIENTS) < 0) {
        LOG_ERROR("Failed to listen: %s", strerror(errno));
        close(server->server_socket);
        free(server);
        server = NULL;
        return -1;
    }
    
    // Inicializar mutex
    if (pthread_mutex_init(&server->clients_mutex, NULL) != 0) {
        LOG_ERROR("Failed to initialize mutex");
        close(server->server_socket);
        free(server);
        server = NULL;
        return -1;
    }
    
    server->running = 1;
    
    // Criar thread de accept
    if (pthread_create(&server->accept_thread, NULL, accept_thread, NULL) != 0) {
        LOG_ERROR("Failed to create accept thread");
        pthread_mutex_destroy(&server->clients_mutex);
        close(server->server_socket);
        free(server);
        server = NULL;
        return -1;
    }
    
    LOG_INFO("RTMP server started on port %d", port);
    return 0;
}

void rtmp_server_stop(void) {
    if (!server) return;
    
    LOG_INFO("Stopping RTMP server...");
    
    server->running = 0;
    
    // Fechar socket principal
    if (server->server_socket >= 0) {
        close(server->server_socket);
        server->server_socket = -1;
    }
    
    // Esperar thread de accept
    pthread_join(server->accept_thread, NULL);
    
    // Limpar clientes
    pthread_mutex_lock(&server->clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (server->clients[i]) {
            cleanup_client(server->clients[i]);
            server->clients[i] = NULL;
        }
    }
    pthread_mutex_unlock(&server->clients_mutex);
    
    pthread_mutex_destroy(&server->clients_mutex);
    
    free(server);
    server = NULL;
    
    LOG_INFO("RTMP server stopped");
}

void rtmp_set_video_callback(RTMPContext* ctx, RTMPVideoCallback cb, void* userdata) {
    if (!ctx) return;
    ctx->video_callback = cb;
    ctx->video_userdata = userdata;
}

void rtmp_set_audio_callback(RTMPContext* ctx, RTMPAudioCallback cb, void* userdata) {
    if (!ctx) return;
    ctx->audio_callback = cb;
    ctx->audio_userdata = userdata;
}

void rtmp_set_event_callback(RTMPContext* ctx, RTMPEventCallback cb, void* userdata) {
    if (!ctx) return;
    ctx->event_callback = cb;
    ctx->event_userdata = userdata;
}

int rtmp_process_packet(RTMPContext* ctx, const uint8_t* data, size_t len) {
    if (!ctx || !data || !len) return -1;
    
    memcpy(ctx->buffer + ctx->buffer_used, data, len);
    ctx->buffer_used += len;
    
    // Processar pacotes completos
    while (ctx->buffer_used > 0) {
        RTMPChunk chunk = {0};
        size_t bytes_read;
        
        if (rtmp_chunk_read(ctx->buffer, ctx->buffer_used, &chunk, &bytes_read) < 0) {
            break;
        }
        
        // Processar chunk
        rtmp_chunk_process(ctx, &chunk);
        
        // Remover dados processados do buffer
        if (bytes_read > 0 && bytes_read <= ctx->buffer_used) {
            memmove(ctx->buffer, ctx->buffer + bytes_read, ctx->buffer_used - bytes_read);
            ctx->buffer_used -= bytes_read;
        }
        
        rtmp_chunk_destroy(&chunk);
    }
    
    return 0;
}