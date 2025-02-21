// rtmp_core.c
#include "rtmp_core.h"
#include <stdarg.h>

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

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

void rtmp_client_handle(RTMPClient* client) {
    if (!client || !client->buffer) return;

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client->addr.sin_addr, client_ip, sizeof(client_ip));
    rtmp_log(RTMP_LOG_INFO, "Handling client %s:%d", client_ip, ntohs(client->addr.sin_port));

    while (client->connected) {
        ssize_t bytes_read = recv(client->socket_fd, client->buffer, RTMP_BUFFER_SIZE, 0);
        
        if (bytes_read > 0) {
            rtmp_log(RTMP_LOG_DEBUG, "Received %zd bytes from client", bytes_read);
            // TODO: Processar dados RTMP
        } 
        else if (bytes_read == 0) {
            rtmp_log(RTMP_LOG_INFO, "Client disconnected");
            break;
        }
        else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                rtmp_log(RTMP_LOG_ERROR, "Error reading from client: %s", strerror(errno));
                break;
            }
        }
        
        usleep(1000); // Pequena pausa para não sobrecarregar CPU
    }

    client->connected = false;
}

static void* client_thread(void* arg) {
    RTMPClient* client = (RTMPClient*)arg;
    rtmp_client_handle(client);
    return NULL;
}

int rtmp_server_add_client(RTMPServer* server, int client_fd, struct sockaddr_in addr) {
    pthread_mutex_lock(&server->clients_mutex);
    
    if (server->client_count >= RTMP_MAX_CONNECTIONS) {
        pthread_mutex_unlock(&server->clients_mutex);
        return -1;
    }

    int index = server->client_count++;
    server->clients[index].socket_fd = client_fd;
    server->clients[index].addr = addr;
    server->clients[index].connected = true;
    server->clients[index].buffer = malloc(RTMP_BUFFER_SIZE);

    if (!server->clients[index].buffer) {
        pthread_mutex_unlock(&server->clients_mutex);
        return -1;
    }

    if (pthread_create(&server->clients[index].thread, NULL, client_thread, &server->clients[index]) != 0) {
        free(server->clients[index].buffer);
        pthread_mutex_unlock(&server->clients_mutex);
        return -1;
    }

    pthread_mutex_unlock(&server->clients_mutex);
    return index;
}

void rtmp_server_remove_client(RTMPServer* server, int client_index) {
    pthread_mutex_lock(&server->clients_mutex);
    
    if (client_index < 0 || client_index >= server->client_count) {
        pthread_mutex_unlock(&server->clients_mutex);
        return;
    }

    RTMPClient* client = &server->clients[client_index];
    client->connected = false;
    pthread_join(client->thread, NULL);
    close(client->socket_fd);
    free(client->buffer);

    // Remover cliente movendo o último para sua posição
    if (client_index < server->client_count - 1) {
        memcpy(&server->clients[client_index], 
               &server->clients[server->client_count - 1], 
               sizeof(RTMPClient));
    }
    server->client_count--;

    pthread_mutex_unlock(&server->clients_mutex);
}

static void* accept_thread(void* arg) {
    RTMPServer* server = (RTMPServer*)arg;
    rtmp_log(RTMP_LOG_INFO, "Accept thread started");

    while (server->running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        fd_set readfds;
        struct timeval tv;
        
        FD_ZERO(&readfds);
        FD_SET(server->socket_fd, &readfds);
        
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int ret = select(server->socket_fd + 1, &readfds, NULL, NULL, &tv);
        
        if (ret < 0) {
            if (errno != EINTR) {
                rtmp_log(RTMP_LOG_ERROR, "Select error: %s", strerror(errno));
            }
            continue;
        }
        
        if (ret > 0) {
            int client_fd = accept(server->socket_fd, (struct sockaddr*)&client_addr, &addr_len);
            if (client_fd < 0) {
                rtmp_log(RTMP_LOG_ERROR, "Accept failed: %s", strerror(errno));
                continue;
            }
            
            // Configurar socket não-bloqueante
            int flags = fcntl(client_fd, F_GETFL, 0);
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
            
            // Adicionar cliente
            if (rtmp_server_add_client(server, client_fd, client_addr) < 0) {
                rtmp_log(RTMP_LOG_ERROR, "Failed to add client");
                close(client_fd);
                continue;
            }
            
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
            rtmp_log(RTMP_LOG_INFO, "New client connected from %s:%d", 
                     client_ip, ntohs(client_addr.sin_port));
        }
    }
    
    rtmp_log(RTMP_LOG_INFO, "Accept thread stopped");
    return NULL;
}

RTMPServer* rtmp_server_create(int port) {
    RTMPServer* server = (RTMPServer*)calloc(1, sizeof(RTMPServer));
    if (!server) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to allocate server structure");
        return NULL;
    }

    server->clients = (RTMPClient*)calloc(RTMP_MAX_CONNECTIONS, sizeof(RTMPClient));
    if (!server->clients) {
        free(server);
        rtmp_log(RTMP_LOG_ERROR, "Failed to allocate clients array");
        return NULL;
    }

    server->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->socket_fd < 0) {
        free(server->clients);
        free(server);
        rtmp_log(RTMP_LOG_ERROR, "Failed to create socket: %s", strerror(errno));
        return NULL;
    }

    // Permitir reuso do endereço/porta
    int opt = 1;
    setsockopt(server->socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Configurar socket não-bloqueante
    int flags = fcntl(server->socket_fd, F_GETFL, 0);
    fcntl(server->socket_fd, F_SETFL, flags | O_NONBLOCK);

    memset(&server->addr, 0, sizeof(server->addr));
    server->addr.sin_family = AF_INET;
    server->addr.sin_addr.s_addr = INADDR_ANY;
    server->addr.sin_port = htons(port);

    pthread_mutex_init(&server->clients_mutex, NULL);

    return server;
}

int rtmp_server_start(RTMPServer* server) {
    if (!server) return RTMP_ERROR_MEMORY;

    if (bind(server->socket_fd, (struct sockaddr*)&server->addr, sizeof(server->addr)) < 0) {
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
    
    // Esperar thread de aceitação terminar
    pthread_join(server->accept_thread, NULL);
    
    // Fechar todos os clientes
    pthread_mutex_lock(&server->clients_mutex);
    for (int i = 0; i < server->client_count; i++) {
        server->clients[i].connected = false;
        pthread_join(server->clients[i].thread, NULL);
        close(server->clients[i].socket_fd);
        free(server->clients[i].buffer);
    }
    pthread_mutex_unlock(&server->clients_mutex);
    
    close(server->socket_fd);
}

void rtmp_server_destroy(RTMPServer* server) {
    if (!server) return;

    rtmp_server_stop(server);
    pthread_mutex_destroy(&server->clients_mutex);
    free(server->clients);
    free(server);
}