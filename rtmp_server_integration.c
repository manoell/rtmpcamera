#include "rtmp_server_integration.h"
#include "rtmp_core.h"
#include "rtmp_protocol.h"
#include "rtmp_diagnostics.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#define RTMP_SERVER_MAX_CLIENTS 8
#define RTMP_SERVER_BUFFER_SIZE 65536
#define RTMP_SERVER_LISTEN_BACKLOG 5
#define RTMP_PACKET_TYPE_CHUNK_SIZE 1
#define RTMP_PACKET_TYPE_AUDIO 8
#define RTMP_PACKET_TYPE_VIDEO 9

typedef struct {
    int socket;
    bool in_use;
    RTMPContext *rtmp_context;
    bool stream_started;
} ClientConnection;

struct RTMPServerContext {
    int server_socket;
    bool running;
    uint16_t port;
    char stream_path[256];
    
    // Client management
    ClientConnection clients[RTMP_SERVER_MAX_CLIENTS];
    uint32_t client_count;
    
    // Threading
    pthread_t server_thread;
    pthread_mutex_t mutex;
    
    // Callback
    rtmp_server_callback_t callback;
    void *callback_context;
    
    // Buffers
    uint8_t *read_buffer;
    uint8_t *write_buffer;
};

static void notify_event(RTMPServerContext *server, RTMPServerEventType type, const char *error_msg) {
    if (server->callback) {
        RTMPServerEvent event = {
            .type = type,
            .error_message = error_msg
        };
        server->callback(&event, server->callback_context);
    }
}

static void handle_client_disconnect(RTMPServerContext *server, int client_index) {
    pthread_mutex_lock(&server->mutex);
    
    if (server->clients[client_index].in_use) {
        if (server->clients[client_index].stream_started) {
            notify_event(server, RTMP_SERVER_EVENT_STREAM_ENDED, NULL);
        }
        
        close(server->clients[client_index].socket);
        if (server->clients[client_index].rtmp_context) {
            rtmp_core_destroy(server->clients[client_index].rtmp_context);
        }
        
        server->clients[client_index].in_use = false;
        server->clients[client_index].stream_started = false;
        server->client_count--;
        
        notify_event(server, RTMP_SERVER_EVENT_CLIENT_DISCONNECTED, NULL);
    }
    
    pthread_mutex_unlock(&server->mutex);
}

static int find_free_client_slot(RTMPServerContext *server) {
    for (int i = 0; i < RTMP_SERVER_MAX_CLIENTS; i++) {
        if (!server->clients[i].in_use) {
            return i;
        }
    }
    return -1;
}

static void handle_client_data(RTMPServerContext *server, int client_index) {
    ssize_t bytes_read = recv(server->clients[client_index].socket,
                             server->read_buffer,
                             RTMP_SERVER_BUFFER_SIZE, 0);
    
    if (bytes_read <= 0) {
        handle_client_disconnect(server, client_index);
        return;
    }
    
    // Process RTMP data
    RTMPPacket *packet = rtmp_chunk_parse(server->read_buffer, bytes_read);
    if (!packet) return;
    
    // Handle packet
    switch (packet->m_packetType) {
        case RTMP_PACKET_TYPE_CHUNK_SIZE:
            rtmp_core_set_chunk_size(server->clients[client_index].rtmp_context,
                                   rtmp_protocol_get_chunk_size(packet));
            break;
            
        case RTMP_PACKET_TYPE_AUDIO:
        case RTMP_PACKET_TYPE_VIDEO:
            // Notify stream started if first media packet
            if (!server->clients[client_index].stream_started) {
                server->clients[client_index].stream_started = true;
                notify_event(server, RTMP_SERVER_EVENT_STREAM_STARTED, NULL);
            }
            break;
    }
    
    rtmp_packet_free(packet);
}

static void* server_thread(void *arg) {
    RTMPServerContext *server = (RTMPServerContext*)arg;
    fd_set read_fds;
    struct timeval tv;
    
    while (server->running) {
        FD_ZERO(&read_fds);
        FD_SET(server->server_socket, &read_fds);
        
        int max_fd = server->server_socket;
        
        // Add client sockets to set
        for (int i = 0; i < RTMP_SERVER_MAX_CLIENTS; i++) {
            if (server->clients[i].in_use) {
                FD_SET(server->clients[i].socket, &read_fds);
                if (server->clients[i].socket > max_fd) {
                    max_fd = server->clients[i].socket;
                }
            }
        }
        
        // Set timeout
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &tv);
        if (activity < 0 && errno != EINTR) {
            notify_event(server, RTMP_SERVER_EVENT_ERROR, "Select failed");
            continue;
        }
        
        // Check for new connections
        if (FD_ISSET(server->server_socket, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            
            int client_socket = accept(server->server_socket,
                                     (struct sockaddr*)&client_addr,
                                     &addr_len);
            
            if (client_socket >= 0) {
                pthread_mutex_lock(&server->mutex);
                
                int client_index = find_free_client_slot(server);
                if (client_index >= 0) {
                    // Configure socket
                    int flag = 1;
                    setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY,
                              &flag, sizeof(flag));
                    
                    // Store client
                    server->clients[client_index].socket = client_socket;
                    server->clients[client_index].in_use = true;
                    server->clients[client_index].rtmp_context = rtmp_core_create();
                    server->clients[client_index].stream_started = false;
                    server->client_count++;
                    
                    notify_event(server, RTMP_SERVER_EVENT_CLIENT_CONNECTED, NULL);
                } else {
                    close(client_socket);
                    notify_event(server, RTMP_SERVER_EVENT_ERROR, "Maximum clients reached");
                }
                
                pthread_mutex_unlock(&server->mutex);
            }
        }
        
        // Check client sockets
        for (int i = 0; i < RTMP_SERVER_MAX_CLIENTS; i++) {
            if (server->clients[i].in_use &&
                FD_ISSET(server->clients[i].socket, &read_fds)) {
                handle_client_data(server, i);
            }
        }
    }
    
    return NULL;
}

RTMPServerContext* rtmp_server_create(void) {
    RTMPServerContext *server = (RTMPServerContext*)calloc(1, sizeof(RTMPServerContext));
    if (!server) return NULL;
    
    server->read_buffer = (uint8_t*)malloc(RTMP_SERVER_BUFFER_SIZE);
    server->write_buffer = (uint8_t*)malloc(RTMP_SERVER_BUFFER_SIZE);
    
    if (!server->read_buffer || !server->write_buffer) {
        rtmp_server_destroy(server);
        return NULL;
    }
    
    server->server_socket = -1;
    pthread_mutex_init(&server->mutex, NULL);
    
    return server;
}

void rtmp_server_destroy(RTMPServerContext *server) {
    if (!server) return;
    
    // Stop server
    server->running = false;
    
    // Close server socket
    if (server->server_socket >= 0) {
        close(server->server_socket);
    }
    
    // Wait for server thread
    if (server->running) {
        pthread_join(server->server_thread, NULL);
    }
    
    // Close all client connections
    for (int i = 0; i < RTMP_SERVER_MAX_CLIENTS; i++) {
        if (server->clients[i].in_use) {
            handle_client_disconnect(server, i);
        }
    }
    
    // Cleanup resources
    pthread_mutex_destroy(&server->mutex);
    free(server->read_buffer);
    free(server->write_buffer);
    free(server);
}

bool rtmp_server_configure(RTMPServerContext *server, const char *port, const char *stream_path) {
    if (!server || !port || !stream_path) return false;
    
    server->port = (uint16_t)atoi(port);
    strncpy(server->stream_path, stream_path, sizeof(server->stream_path) - 1);
    
    return true;
}

bool rtmp_server_start(RTMPServerContext *server) {
    if (!server) return false;
    
    // Create socket
    server->server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server->server_socket < 0) {
        notify_event(server, RTMP_SERVER_EVENT_ERROR, "Failed to create server socket");
        return false;
    }
    
    // Set socket options
    int flag = 1;
    if (setsockopt(server->server_socket, SOL_SOCKET, SO_REUSEADDR,
                   &flag, sizeof(flag)) < 0) {
        notify_event(server, RTMP_SERVER_EVENT_ERROR, "Failed to set socket options");
        close(server->server_socket);
        return false;
    }
    
    // Bind
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server->port);
    
    if (bind(server->server_socket, (struct sockaddr*)&server_addr,
             sizeof(server_addr)) < 0) {
        notify_event(server, RTMP_SERVER_EVENT_ERROR, "Failed to bind server socket");
        close(server->server_socket);
        return false;
    }
    
    // Listen
    if (listen(server->server_socket, RTMP_SERVER_LISTEN_BACKLOG) < 0) {
        notify_event(server, RTMP_SERVER_EVENT_ERROR, "Failed to listen on server socket");
        close(server->server_socket);
        return false;
    }
    
    // Start server thread
    server->running = true;
    if (pthread_create(&server->server_thread, NULL, server_thread, server) != 0) {
        notify_event(server, RTMP_SERVER_EVENT_ERROR, "Failed to create server thread");
        close(server->server_socket);
        server->running = false;
        return false;
    }
    
    notify_event(server, RTMP_SERVER_EVENT_STARTED, NULL);
    return true;
}

void rtmp_server_set_callback(RTMPServerContext *server,
                            rtmp_server_callback_t callback,
                            void *context) {
    if (!server) return;
    
    pthread_mutex_lock(&server->mutex);
    server->callback = callback;
    server->callback_context = context;
    pthread_mutex_unlock(&server->mutex);
}

bool rtmp_server_is_running(RTMPServerContext *server) {
    return server ? server->running : false;
}

uint32_t rtmp_server_get_client_count(RTMPServerContext *server) {
    return server ? server->client_count : 0;
}