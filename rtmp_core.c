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
#include <errno.h>

// Configurações
#define RTMP_MAX_CLIENTS 10
#define RTMP_BUFFER_SIZE 4096
#define RTMP_LOG_FILE "/tmp/rtmpcamera.log"

// Estrutura do servidor
typedef struct {
    int running;
    int server_socket;
    pthread_t accept_thread;
    RTMPSession* sessions[RTMP_MAX_CLIENTS];
    pthread_mutex_t sessions_mutex;
    RTMPConfig config;
    FILE* log_file;
} RTMPServer;

static RTMPServer* g_server = NULL;

// Função de logging
static void rtmp_log(const char* format, ...) {
    if (!g_server || !g_server->log_file) return;
    
    va_list args;
    va_start(args, format);
    
    time_t now;
    time(&now);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    fprintf(g_server->log_file, "[%s] ", timestamp);
    vfprintf(g_server->log_file, format, args);
    fprintf(g_server->log_file, "\n");
    fflush(g_server->log_file);
    
    va_end(args);
}

// Gerenciamento de sessões
static int add_session(RTMPSession* session) {
    if (!g_server) return -1;
    
    pthread_mutex_lock(&g_server->sessions_mutex);
    
    int slot = -1;
    for (int i = 0; i < RTMP_MAX_CLIENTS; i++) {
        if (!g_server->sessions[i]) {
            g_server->sessions[i] = session;
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
            break;
        }
    }
    
    pthread_mutex_unlock(&g_server->sessions_mutex);
}

static void cleanup_session(RTMPSession* session) {
    if (!session) return;
    
    rtmp_log("Cleaning up session %p", session);
    
    if (session->socket >= 0) {
        close(session->socket);
    }
    
    remove_session(session);
    
    if (session->buffer) {
        free(session->buffer);
    }
    
    if (session->handshake_data) {
        free(session->handshake_data);
    }
    
    free(session);
}

// Handler de cliente
static void* client_handler(void* arg) {
    RTMPSession* session = (RTMPSession*)arg;
    
    rtmp_log("New client connected from %s", session->ip_address);
    
    // Configura buffer
    session->buffer = malloc(RTMP_BUFFER_SIZE);
    session->buffer_size = RTMP_BUFFER_SIZE;
    
    // Handshake
    if (rtmp_handshake_process(session) < 0) {
        rtmp_log("Handshake failed for session %p", session);
        cleanup_session(session);
        return NULL;
    }
    
    rtmp_log("Handshake successful for session %p", session);
    
    // Loop principal
    while (session->running && g_server->running) {
        ssize_t bytes = recv(session->socket, 
                           session->buffer + session->buffer_offset,
                           session->buffer_size - session->buffer_offset, 
                           0);
        
        if (bytes <= 0) {
            if (bytes < 0) {
                rtmp_log("Receive error on session %p: %s", session, strerror(errno));
            }
            break;
        }
        
        session->buffer_offset += bytes;
        
        // Processa chunks RTMP
        size_t processed = 0;
        while (processed < session->buffer_offset) {
            RTMPChunk chunk;
            int ret = rtmp_chunk_parse(session->buffer + processed,
                                     session->buffer_offset - processed,
                                     &chunk);
            
            if (ret < 0) break;
            
            processed += ret;
            
            // Processa mensagem
            if (rtmp_protocol_handle_message(session, &chunk) < 0) {
                rtmp_log("Error processing message for session %p", session);
                goto cleanup;
            }
        }
        
        // Move dados não processados para o início do buffer
        if (processed < session->buffer_offset) {
            memmove(session->buffer, 
                    session->buffer + processed,
                    session->buffer_offset - processed);
        }
        session->buffer_offset -= processed;
    }
    
cleanup:
    rtmp_log("Client disconnected from session %p", session);
    cleanup_session(session);
    return NULL;
}

// Handler de aceitação
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
        
        // Cria nova sessão
        RTMPSession* session = (RTMPSession*)calloc(1, sizeof(RTMPSession));
        session->socket = client_socket;
        session->running = 1;
        
        // Guarda endereço IP do cliente
        inet_ntop(AF_INET, &client_addr.sin_addr,
                 session->ip_address, sizeof(session->ip_address));
        
        // Adiciona à lista de sessões
        if (add_session(session) < 0) {
            rtmp_log("Max clients reached, rejecting connection from %s",
                    session->ip_address);
            cleanup_session(session);
            continue;
        }
        
        // Inicia thread do cliente
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

// API pública
int rtmp_server_init(const RTMPConfig* config) {
    if (g_server) return -1;
    
    g_server = (RTMPServer*)calloc(1, sizeof(RTMPServer));
    
    // Abre arquivo de log
    g_server->log_file = fopen(RTMP_LOG_FILE, "a");
    if (!g_server->log_file) {
        free(g_server);
        g_server = NULL;
        return -1;
    }
    
    // Copia configuração
    if (config) {
        memcpy(&g_server->config, config, sizeof(RTMPConfig));
    }
    
    pthread_mutex_init(&g_server->sessions_mutex, NULL);
    
    rtmp_log("RTMP server initialized");
    return 0;
}

int rtmp_server_start(void) {
    if (!g_server) return -1;
    
    // Cria socket
    g_server->server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (g_server->server_socket < 0) {
        rtmp_log("Failed to create server socket: %s", strerror(errno));
        return -1;
    }
    
    // Configura socket
    int reuse = 1;
    setsockopt(g_server->server_socket, SOL_SOCKET, SO_REUSEADDR,
               &reuse, sizeof(reuse));
    
    // Configura endereço
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(g_server->config.port ? 
                         g_server->config.port : 1935);
    
    // Bind
    if (bind(g_server->server_socket, (struct sockaddr*)&addr,
             sizeof(addr)) < 0) {
        rtmp_log("Failed to bind server socket: %s", strerror(errno));
        close(g_server->server_socket);
        return -1;
    }
    
    // Listen
    if (listen(g_server->server_socket, 5) < 0) {
        rtmp_log("Failed to listen on server socket: %s", strerror(errno));
        close(g_server->server_socket);
        return -1;
    }
    
    g_server->running = 1;
    
    // Inicia thread de aceitação
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
    
    // Fecha socket do servidor
    if (g_server->server_socket >= 0) {
        close(g_server->server_socket);
    }
    
    // Espera thread de accept
    pthread_join(g_server->accept_thread, NULL);
    
    // Limpa sessões
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