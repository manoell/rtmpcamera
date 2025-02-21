#include "rtmp_core.h"
#include <stdarg.h>
#include <time.h>

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

static RTMPClient* rtmp_client_create(int socket_fd, struct sockaddr_in addr) {
    RTMPClient* client = (RTMPClient*)calloc(1, sizeof(RTMPClient));
    if (!client) return NULL;

    client->socket_fd = socket_fd;
    client->addr = addr;
    client->state = RTMP_STATE_HANDSHAKE_S0S1;
    client->running = true;

    client->in_buffer = (uint8_t*)malloc(RTMP_BUFFER_SIZE);
    client->out_buffer = (uint8_t*)malloc(RTMP_BUFFER_SIZE);

    if (!client->in_buffer || !client->out_buffer) {
        free(client->in_buffer);
        free(client->out_buffer);
        free(client);
        return NULL;
    }

    return client;
}

static void rtmp_client_destroy(RTMPClient* client) {
    if (!client) return;

    client->running = false;
    if (client->socket_fd >= 0) {
        close(client->socket_fd);
    }
    
    pthread_join(client->thread, NULL);
    
    free(client->in_buffer);
    free(client->out_buffer);
    free(client);
}

static void* handle_client(void *arg) {
    RTMPClient *client = (RTMPClient *)arg;
    rtmp_log(RTMP_LOG_INFO, "Handling client %s:%d",
             inet_ntoa(client->addr.sin_addr),
             ntohs(client->addr.sin_port));

    if (rtmp_handshake_handle(client) != RTMP_OK) {
        rtmp_log(RTMP_LOG_ERROR, "Handshake failed");
        goto cleanup;
    }

    client->state = RTMP_STATE_CONNECT;
    rtmp_log(RTMP_LOG_INFO, "Client ready for RTMP commands");

    uint8_t buffer[RTMP_BUFFER_SIZE];
    while (client->running) {
        ssize_t bytes = read_with_timeout(client->socket_fd, buffer, sizeof(buffer), 5000); // 5 segundos
        
        if (bytes > 0) {
            rtmp_log(RTMP_LOG_DEBUG, "Received %zd bytes from client", bytes);
            if (rtmp_handle_packet(client, buffer, bytes) != RTMP_OK) {
                rtmp_log(RTMP_LOG_ERROR, "Failed to handle packet");
                break;
            }
        } else if (bytes == 0) {
            rtmp_log(RTMP_LOG_INFO, "Client disconnected");
            break;
        } else {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                rtmp_log(RTMP_LOG_ERROR, "Error reading from client: %s", strerror(errno));
                break;
            }
        }
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
        
        // Usar select para espera não-bloqueante
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
            int client_fd = accept(server->socket_fd, (struct sockaddr*)&client_addr, &addr_len);
            if (client_fd < 0) {
                rtmp_log(RTMP_LOG_ERROR, "Accept failed: %s", strerror(errno));
                continue;
            }
            
            // Configurar socket não-bloqueante
            int flags = fcntl(client_fd, F_GETFL, 0);
            fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
            
            // Criar cliente
            RTMPClient* client = rtmp_client_create(client_fd, client_addr);
            if (!client) {
                rtmp_log(RTMP_LOG_ERROR, "Failed to create client");
                close(client_fd);
                continue;
            }
            
            // Iniciar thread do cliente
            if (pthread_create(&client->thread, NULL, handle_client, client) != 0) {
                rtmp_log(RTMP_LOG_ERROR, "Failed to create client thread");
                rtmp_client_destroy(client);
                continue;
            }
            
            rtmp_log(RTMP_LOG_INFO, "New client connected from %s:%d",
                     inet_ntoa(client_addr.sin_addr),
                     ntohs(client_addr.sin_port));
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

    server->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->socket_fd < 0) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to create socket: %s", strerror(errno));
        free(server);
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
    pthread_join(server->accept_thread, NULL);
    
    // Limpar clientes
    pthread_mutex_lock(&server->clients_mutex);
    for (int i = 0; i < RTMP_MAX_CONNECTIONS; i++) {
        if (server->clients[i]) {
            rtmp_client_destroy(server->clients[i]);
            server->clients[i] = NULL;
        }
    }
    pthread_mutex_unlock(&server->clients_mutex);
    
    close(server->socket_fd);
}

void rtmp_server_destroy(RTMPServer* server) {
    if (!server) return;

    rtmp_server_stop(server);
    pthread_mutex_destroy(&server->clients_mutex);
    free(server);
}