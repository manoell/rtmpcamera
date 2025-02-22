#include "rtmp_core.h"
#include "rtmp_protocol.h"
#include "rtmp_utils.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <fcntl.h>

static char error_message[1024];
static void* rtmp_server_accept_thread(void *arg);
static void* rtmp_session_thread(void *arg);

rtmp_server_t* rtmp_server_create(void) {
    rtmp_server_t *server = (rtmp_server_t*)calloc(1, sizeof(rtmp_server_t));
    if (!server) {
        rtmp_set_error("Failed to allocate server structure");
        return NULL;
    }
    
    server->socket_fd = -1;
    server->running = 0;
    server->chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
    server->window_size = RTMP_DEFAULT_WINDOW_SIZE;
    server->peer_bandwidth = RTMP_DEFAULT_WINDOW_SIZE;
    
    if (pthread_mutex_init(&server->connections_mutex, NULL) != 0) {
        rtmp_set_error("Failed to initialize connections mutex");
        free(server);
        return NULL;
    }
    
    return server;
}

int rtmp_server_configure(rtmp_server_t *server, const rtmp_server_config_t *config) {
    if (!server || !config) {
        rtmp_set_error("Invalid server or config");
        return -1;
    }
    
    // Validate configuration
    if (config->port <= 0 || config->port > 65535) {
        rtmp_set_error("Invalid port number");
        return -1;
    }
    
    if (config->chunk_size < 128 || config->chunk_size > 65536) {
        rtmp_set_error("Invalid chunk size");
        return -1;
    }
    
    if (config->window_size < 1024 || config->window_size > 5000000) {
        rtmp_set_error("Invalid window size");
        return -1;
    }
    
    // Apply configuration
    server->port = config->port;
    server->chunk_size = config->chunk_size;
    server->window_size = config->window_size;
    server->peer_bandwidth = config->peer_bandwidth;
    
    return 0;
}

void rtmp_server_set_callbacks(rtmp_server_t *server,
                             rtmp_client_callback on_connect,
                             rtmp_client_callback on_disconnect,
                             rtmp_stream_callback on_publish,
                             rtmp_stream_callback on_play) {
    if (!server) return;
    
    server->on_client_connect = on_connect;
    server->on_client_disconnect = on_disconnect;
    server->on_publish_stream = on_publish;
    server->on_play_stream = on_play;
}

int rtmp_server_start(rtmp_server_t *server) {
    if (!server) {
        rtmp_set_error("Invalid server");
        return -1;
    }
    
    if (server->running) {
        rtmp_set_error("Server already running");
        return -1;
    }
    
    // Create socket
    server->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->socket_fd < 0) {
        rtmp_set_error("Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    // Set socket options
    int reuse = 1;
    if (setsockopt(server->socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        rtmp_set_error("Failed to set SO_REUSEADDR: %s", strerror(errno));
        close(server->socket_fd);
        return -1;
    }
    
    // Set non-blocking mode
    int flags = fcntl(server->socket_fd, F_GETFL, 0);
    if (flags < 0 || fcntl(server->socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        rtmp_set_error("Failed to set non-blocking mode: %s", strerror(errno));
        close(server->socket_fd);
        return -1;
    }
    
    // Bind socket
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(server->port);
    
    if (bind(server->socket_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        rtmp_set_error("Failed to bind socket: %s", strerror(errno));
        close(server->socket_fd);
        return -1;
    }
    
    // Start listening
    if (listen(server->socket_fd, 5) < 0) {
        rtmp_set_error("Failed to listen: %s", strerror(errno));
        close(server->socket_fd);
        return -1;
    }
    
    // Start accept thread
    server->running = 1;
    if (pthread_create(&server->accept_thread, NULL, rtmp_server_accept_thread, server) != 0) {
        rtmp_set_error("Failed to create accept thread: %s", strerror(errno));
        close(server->socket_fd);
        server->running = 0;
        return -1;
    }
    
    return 0;
}

static void* rtmp_server_accept_thread(void *arg) {
    rtmp_server_t *server = (rtmp_server_t *)arg;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    
    while (server->running) {
        // Accept new connection
        int client_fd = accept(server->socket_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // No pending connections, sleep briefly
                usleep(1000);
                continue;
            }
            if (server->running) {
                rtmp_set_error("Failed to accept client: %s", strerror(errno));
            }
            continue;
        }
        
        // Set TCP_NODELAY
        int nodelay = 1;
        setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
        
        // Create new session
        rtmp_session_t *session = rtmp_session_create(client_fd);
        if (!session) {
            close(client_fd);
            continue;
        }
        
        session->server = server;
        
        // Add to connections list
        pthread_mutex_lock(&server->connections_mutex);
        if (server->num_connections >= RTMP_MAX_CONNECTIONS) {
            pthread_mutex_unlock(&server->connections_mutex);
            rtmp_session_destroy(session);
            continue;
        }
        server->connections[server->num_connections++] = session;
        pthread_mutex_unlock(&server->connections_mutex);
        
        // Notify callback
        if (server->on_client_connect) {
            server->on_client_connect(server, session);
        }
        
        // Start session thread
        pthread_t thread;
        if (pthread_create(&thread, NULL, rtmp_session_thread, session) != 0) {
            rtmp_set_error("Failed to create session thread: %s", strerror(errno));
            pthread_mutex_lock(&server->connections_mutex);
            server->num_connections--;
            pthread_mutex_unlock(&server->connections_mutex);
            rtmp_session_destroy(session);
            continue;
        }
        pthread_detach(thread);
    }
    
    return NULL;
}

int rtmp_server_stop(rtmp_server_t *server) {
    if (!server) {
        rtmp_set_error("Invalid server");
        return -1;
    }
    
    if (!server->running) {
        return 0;
    }
    
    // Stop accept thread
    server->running = 0;
    shutdown(server->socket_fd, SHUT_RDWR);
    close(server->socket_fd);
    server->socket_fd = -1;
    
    pthread_join(server->accept_thread, NULL);
    
    // Close all connections
    pthread_mutex_lock(&server->connections_mutex);
    for (int i = 0; i < server->num_connections; i++) {
        rtmp_session_t *session = server->connections[i];
        if (session) {
            if (server->on_client_disconnect) {
                server->on_client_disconnect(server, session);
            }
            rtmp_session_destroy(session);
        }
    }
    server->num_connections = 0;
    pthread_mutex_unlock(&server->connections_mutex);
    
    return 0;
}

void rtmp_server_destroy(rtmp_server_t *server) {
    if (!server) return;
    
    if (server->running) {
        rtmp_server_stop(server);
    }
    
    pthread_mutex_destroy(&server->connections_mutex);
    free(server);
}

static void* rtmp_session_thread(void *arg) {
    rtmp_session_t *session = (rtmp_session_t *)arg;
    rtmp_server_t *server = session->server;
    
    // Perform handshake
    if (rtmp_handshake_server(session) < 0) {
        goto cleanup;
    }
    
    // Main processing loop
    uint8_t buffer[4096];
    ssize_t bytes_read;
    
    while ((bytes_read = recv(session->socket_fd, buffer, sizeof(buffer), 0)) > 0) {
        if (rtmp_session_process_input(session, buffer, bytes_read) < 0) {
            break;
        }
    }
    
cleanup:
    // Remove from connections list
    pthread_mutex_lock(&server->connections_mutex);
    for (int i = 0; i < server->num_connections; i++) {
        if (server->connections[i] == session) {
            memmove(&server->connections[i], 
                    &server->connections[i + 1],
                    (server->num_connections - i - 1) * sizeof(rtmp_session_t*));
            server->num_connections--;
            break;
        }
    }
    pthread_mutex_unlock(&server->connections_mutex);
    
    // Notify callback
    if (server->on_client_disconnect) {
        server->on_client_disconnect(server, session);
    }
    
    rtmp_session_destroy(session);
    return NULL;
}

void rtmp_set_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(error_message, sizeof(error_message), fmt, args);
    va_end(args);
}

const char* rtmp_get_error(void) {
    return error_message;
}