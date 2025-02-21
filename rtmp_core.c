#include "rtmp_core.h"
#include "rtmp_handshake.h"
#include "rtmp_protocol.h"
#include <stdarg.h>
#include <time.h>

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// Implementação da função de log thread-safe
void rtmp_log(RTMPLogLevel level, const char* format, ...) {
    pthread_mutex_lock(&log_mutex);
    
    FILE* fp = fopen(RTMP_LOG_FILE, "a");
    if (!fp) {
        pthread_mutex_unlock(&log_mutex);
        return;
    }

    time_t now;
    time(&now);
    struct tm* timeinfo = localtime(&now);
    char timestamp[26];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);

    const char* level_str;
    switch (level) {
        case RTMP_LOG_DEBUG:   level_str = "DEBUG"; break;
        case RTMP_LOG_INFO:    level_str = "INFO"; break;
        case RTMP_LOG_WARNING: level_str = "WARNING"; break;
        case RTMP_LOG_ERROR:   level_str = "ERROR"; break;
    }

    fprintf(fp, "[%s] [%s] ", timestamp, level_str);

    va_list args;
    va_start(args, format);
    vfprintf(fp, format, args);
    va_end(args);
    fprintf(fp, "\n");

    fclose(fp);
    pthread_mutex_unlock(&log_mutex);
}

static RTMPClient* rtmp_client_create(int socket_fd, struct sockaddr_in addr) {
    RTMPClient* client = (RTMPClient*)calloc(1, sizeof(RTMPClient));
    if (!client) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to allocate client");
        return NULL;
    }

    client->socket_fd = socket_fd;
    client->addr = addr;
    client->state = RTMP_STATE_UNINITIALIZED;
    client->running = true;
    client->chunk_size = 128; // Tamanho padrão do chunk
    client->window_size = 2500000; // 2.5MB window size
    client->bandwidth = 2500000; // 2.5MB bandwidth
    client->bandwidth_limit_type = 2; // Dynamic bandwidth
    
    // Alocar buffers
    client->in_buffer = (uint8_t*)malloc(client->window_size);
    client->out_buffer = (uint8_t*)malloc(client->window_size);
    
    if (!client->in_buffer || !client->out_buffer) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to allocate client buffers");
        free(client->in_buffer);
        free(client->out_buffer);
        free(client);
        return NULL;
    }

    // Configurar socket não-bloqueante
    int flags = fcntl(socket_fd, F_GETFL, 0);
    fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
    
    // Configurar socket TCP
    int opt = 1;
    setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    rtmp_log(RTMP_LOG_DEBUG, "Client created");
    return client;
}

static void rtmp_client_destroy(RTMPClient* client) {
    if (!client) return;

    client->running = false;
    if (client->socket_fd >= 0) {
        close(client->socket_fd);
    }
    
    free(client->in_buffer);
    free(client->out_buffer);
    
    if (client->current_message) {
        free(client->current_message->payload);
        free(client->current_message);
    }
    
    pthread_join(client->thread, NULL);
    free(client);
    
    rtmp_log(RTMP_LOG_DEBUG, "Client destroyed");
}

static void* client_handler(void* arg) {
    RTMPClient* client = (RTMPClient*)arg;
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->addr.sin_addr, client_ip, sizeof(client_ip));
    
    rtmp_log(RTMP_LOG_INFO, "Handling client %s:%d", 
             client_ip, ntohs(client->addr.sin_port));

    // Handshake RTMP
    if (rtmp_handshake_perform(client) != RTMP_OK) {
        rtmp_log(RTMP_LOG_ERROR, "Handshake failed");
        goto cleanup;
    }
    
    client->state = RTMP_STATE_HANDSHAKE_DONE;
    rtmp_log(RTMP_LOG_INFO, "Client ready for RTMP commands");

    // Loop principal de processamento de mensagens
    while (client->running) {
        RTMPMessage message;
        int ret = rtmp_protocol_read_message(client, &message);
        
        if (ret == RTMP_OK) {
            rtmp_protocol_handle_message(client, &message);
            if (message.payload) {
                free(message.payload);
            }
        } else if (ret != RTMP_ERROR_SOCKET) {
            // Erro fatal
            break;
        }
        
        usleep(1000); // 1ms para não sobrecarregar a CPU
    }

cleanup:
    rtmp_log(RTMP_LOG_INFO, "Client handler exiting");
    rtmp_client_destroy(client);
    return NULL;
}

static void* accept_thread(void* arg) {
    RTMPServer* server = (RTMPServer*)arg;
    rtmp_log(RTMP_LOG_INFO, "Accept thread started");

    while (server->running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        // Select para espera não-bloqueante
        fd_set readfds;
        struct timeval tv;
        
        FD_ZERO(&readfds);
        FD_SET(server->socket_fd, &readfds);
        
        tv.tv_sec = 1;  // 1 segundo timeout
        tv.tv_usec = 0;
        
        int ret = select(server->socket_fd + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno != EINTR) {
                rtmp_log(RTMP_LOG_ERROR, "Select error: %s", strerror(errno));
            }
            continue;
        }
        
        if (ret > 0) {
            int client_fd = accept(server->socket_fd, 
                                 (struct sockaddr*)&client_addr, 
                                 &addr_len);
            
            if (client_fd < 0) {
                rtmp_log(RTMP_LOG_ERROR, "Accept failed: %s", strerror(errno));
                continue;
            }

            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
            rtmp_log(RTMP_LOG_INFO, "New client connected from %s:%d",
                     client_ip, ntohs(client_addr.sin_port));
            
            RTMPClient* client = rtmp_client_create(client_fd, client_addr);
            if (!client) {
                close(client_fd);
                continue;
            }

            pthread_mutex_lock(&server->clients_mutex);
            if (server->client_count < RTMP_MAX_CONNECTIONS) {
                // Encontrar slot livre
                for (int i = 0; i < RTMP_MAX_CONNECTIONS; i++) {
                    if (!server->clients[i]) {
                        server->clients[i] = client;
                        server->client_count++;
                        break;
                    }
                }
                
                // Criar thread do cliente
                if (pthread_create(&client->thread, NULL, client_handler, client) != 0) {
                    rtmp_log(RTMP_LOG_ERROR, "Failed to create client thread");
                    rtmp_client_destroy(client);
                    server->client_count--;
                }
            } else {
                rtmp_log(RTMP_LOG_WARNING, "Maximum connections reached");
                rtmp_client_destroy(client);
            }
            pthread_mutex_unlock(&server->clients_mutex);
        }
    }
    
    rtmp_log(RTMP_LOG_INFO, "Accept thread stopped");
    return NULL;
}

RTMPServer* rtmp_server_create(int port) {
    RTMPServer* server = (RTMPServer*)calloc(1, sizeof(RTMPServer));
    if (!server) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to allocate server");
        return NULL;
    }

    server->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->socket_fd < 0) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to create socket: %s", strerror(errno));
        free(server);
        return NULL;
    }

    // Permitir reuso do endereço/porta
    int opt = 1;
    setsockopt(server->socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Socket não-bloqueante
    int flags = fcntl(server->socket_fd, F_GETFL, 0);
    fcntl(server->socket_fd, F_SETFL, flags | O_NONBLOCK);
    
    // Configurar endereço
    memset(&server->addr, 0, sizeof(server->addr));
    server->addr.sin_family = AF_INET;
    server->addr.sin_addr.s_addr = INADDR_ANY;
    server->addr.sin_port = htons(port);
    
    pthread_mutex_init(&server->clients_mutex, NULL);
    
    rtmp_log(RTMP_LOG_INFO, "RTMP Server created");
    return server;
}

int rtmp_server_start(RTMPServer* server) {
    if (!server) return RTMP_ERROR_MEMORY;

    if (bind(server->socket_fd, (struct sockaddr*)&server->addr, 
             sizeof(server->addr)) < 0) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to bind: %s", strerror(errno));
        return RTMP_ERROR_BIND;
    }

    if (listen(server->socket_fd, RTMP_MAX_CONNECTIONS) < 0) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to listen: %s", strerror(errno));
        return RTMP_ERROR_LISTEN;
    }

    server->running = true;
    
    if (pthread_create(&server->accept_thread, NULL, accept_thread, server) != 0) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to create accept thread");
        return RTMP_ERROR_MEMORY;
    }

    rtmp_log(RTMP_LOG_INFO, "RTMP Server started successfully");
    return RTMP_OK;
}

void rtmp_server_stop(RTMPServer* server) {
    if (!server) return;

    server->running = false;
    pthread_join(server->accept_thread, NULL);
    
    pthread_mutex_lock(&server->clients_mutex);
    for (int i = 0; i < RTMP_MAX_CONNECTIONS; i++) {
        if (server->clients[i]) {
            rtmp_client_destroy(server->clients[i]);
            server->clients[i] = NULL;
        }
    }
    server->client_count = 0;
    pthread_mutex_unlock(&server->clients_mutex);
    
    close(server->socket_fd);
    rtmp_log(RTMP_LOG_INFO, "RTMP Server stopped");
}

void rtmp_server_destroy(RTMPServer* server) {
    if (!server) return;

    rtmp_server_stop(server);
    pthread_mutex_destroy(&server->clients_mutex);
    free(server);
    
    rtmp_log(RTMP_LOG_INFO, "RTMP Server destroyed");
}