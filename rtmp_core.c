#include "rtmp_core.h"
#include "rtmp_protocol.h"
#include "rtmp_handshake.h"
#include "rtmp_utils.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define RTMP_SERVER_PORT 1935
#define MAX_CLIENTS 10
#define BUFFER_SIZE 4096

typedef struct {
    int running;
    int server_socket;
    pthread_t accept_thread;
    RTMPSession* sessions[MAX_CLIENTS];
    pthread_mutex_t sessions_mutex;
    VideoFrameCallback video_callback;
    AudioFrameCallback audio_callback;
} RTMPServer;

static RTMPServer* g_server = NULL;

// Buffer pool para otimizar alocação de memória
typedef struct {
    uint8_t* buffers[32];
    int available[32];
    pthread_mutex_t mutex;
} BufferPool;

static BufferPool* buffer_pool_create() {
    BufferPool* pool = (BufferPool*)malloc(sizeof(BufferPool));
    pthread_mutex_init(&pool->mutex, NULL);
    
    for (int i = 0; i < 32; i++) {
        pool->buffers[i] = (uint8_t*)malloc(BUFFER_SIZE);
        pool->available[i] = 1;
    }
    
    return pool;
}

static void buffer_pool_destroy(BufferPool* pool) {
    if (!pool) return;
    
    for (int i = 0; i < 32; i++) {
        if (pool->buffers[i]) {
            free(pool->buffers[i]);
        }
    }
    
    pthread_mutex_destroy(&pool->mutex);
    free(pool);
}

static uint8_t* buffer_pool_acquire(BufferPool* pool) {
    pthread_mutex_lock(&pool->mutex);
    
    for (int i = 0; i < 32; i++) {
        if (pool->available[i]) {
            pool->available[i] = 0;
            pthread_mutex_unlock(&pool->mutex);
            return pool->buffers[i];
        }
    }
    
    pthread_mutex_unlock(&pool->mutex);
    return (uint8_t*)malloc(BUFFER_SIZE); // Fallback se pool estiver cheio
}

static void buffer_pool_release(BufferPool* pool, uint8_t* buffer) {
    pthread_mutex_lock(&pool->mutex);
    
    for (int i = 0; i < 32; i++) {
        if (pool->buffers[i] == buffer) {
            pool->available[i] = 1;
            pthread_mutex_unlock(&pool->mutex);
            return;
        }
    }
    
    free(buffer); // Se não for do pool, libera normalmente
    pthread_mutex_unlock(&pool->mutex);
}

static void rtmp_session_cleanup(RTMPSession* session) {
    if (!session) return;
    
    if (session->socket >= 0) {
        close(session->socket);
    }
    
    if (session->handshake_data) {
        free(session->handshake_data);
    }
    
    if (session->app) {
        free(session->app);
    }
    
    if (session->streamName) {
        free(session->streamName);
    }
    
    if (session->tcUrl) {
        free(session->tcUrl);
    }
    
    if (session->buffer_pool) {
        buffer_pool_destroy(session->buffer_pool);
    }
    
    free(session);
}

static void* client_handler(void* arg) {
    RTMPSession* session = (RTMPSession*)arg;
    BufferPool* pool = buffer_pool_create();
    session->buffer_pool = pool;
    
    // Handshake
    if (rtmp_handshake_process(session) < 0) {
        rtmp_session_cleanup(session);
        return NULL;
    }
    
    uint8_t* chunk_buffer = buffer_pool_acquire(pool);
    RTMPChunk chunk;
    
    while (session->running) {
        ssize_t bytes = recv(session->socket, chunk_buffer, BUFFER_SIZE, 0);
        if (bytes <= 0) break;
        
        size_t offset = 0;
        while (offset < bytes) {
            int ret = rtmp_chunk_parse(chunk_buffer + offset, bytes - offset, &chunk);
            if (ret < 0) break;
            
            offset += ret;
            
            // Processa a mensagem
            rtmp_protocol_handle_message(session, &chunk);
            
            // Libera payload se alocado
            if (chunk.payload) {
                buffer_pool_release(pool, chunk.payload);
                chunk.payload = NULL;
            }
        }
    }
    
    buffer_pool_release(pool, chunk_buffer);
    rtmp_session_cleanup(session);
    return NULL;
}

static void* accept_handler(void* arg) {
    RTMPServer* server = (RTMPServer*)arg;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    while (server->running) {
        int client_socket = accept(server->server_socket, 
                                 (struct sockaddr*)&client_addr, 
                                 &addr_len);
        
        if (client_socket < 0) continue;
        
        // Cria nova sessão
        RTMPSession* session = (RTMPSession*)malloc(sizeof(RTMPSession));
        memset(session, 0, sizeof(RTMPSession));
        session->socket = client_socket;
        session->running = 1;
        session->video_callback = server->video_callback;
        session->audio_callback = server->audio_callback;
        
        // Adiciona à lista de sessões
        pthread_mutex_lock(&server->sessions_mutex);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!server->sessions[i]) {
                server->sessions[i] = session;
                break;
            }
        }
        pthread_mutex_unlock(&server->sessions_mutex);
        
        // Inicia thread do cliente
        pthread_t thread;
        pthread_create(&thread, NULL, client_handler, session);
        pthread_detach(thread);
    }
    
    return NULL;
}

int rtmp_server_start(int port, VideoFrameCallback video_cb, AudioFrameCallback audio_cb) {
    if (g_server) return -1;
    
    g_server = (RTMPServer*)malloc(sizeof(RTMPServer));
    memset(g_server, 0, sizeof(RTMPServer));
    
    g_server->server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server->server_socket < 0) {
        free(g_server);
        g_server = NULL;
        return -1;
    }
    
    int reuse = 1;
    setsockopt(g_server->server_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port ? port : RTMP_SERVER_PORT);
    
    if (bind(g_server->server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(g_server->server_socket);
        free(g_server);
        g_server = NULL;
        return -1;
    }
    
    if (listen(g_server->server_socket, 5) < 0) {
        close(g_server->server_socket);
        free(g_server);
        g_server = NULL;
        return -1;
    }
    
    g_server->running = 1;
    g_server->video_callback = video_cb;
    g_server->audio_callback = audio_cb;
    pthread_mutex_init(&g_server->sessions_mutex, NULL);
    
    pthread_create(&g_server->accept_thread, NULL, accept_handler, g_server);
    
    return 0;
}

void rtmp_server_stop() {
    if (!g_server) return;
    
    g_server->running = 0;
    
    // Fecha socket do servidor
    if (g_server->server_socket >= 0) {
        close(g_server->server_socket);
    }
    
    // Espera thread de accept terminar
    pthread_join(g_server->accept_thread, NULL);
    
    // Limpa sessões
    pthread_mutex_lock(&g_server->sessions_mutex);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_server->sessions[i]) {
            g_server->sessions[i]->running = 0;
            rtmp_session_cleanup(g_server->sessions[i]);
            g_server->sessions[i] = NULL;
        }
    }
    pthread_mutex_unlock(&g_server->sessions_mutex);
    
    pthread_mutex_destroy(&g_server->sessions_mutex);
    
    free(g_server);
    g_server = NULL;
}