// rtmp_core.c
#include "rtmp_core.h"
#include <stdarg.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <errno.h>

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

struct RTMPServer {
    int port;
    int socket_fd;
    RTMPSession* sessions[RTMP_MAX_CONNECTIONS];
    int num_sessions;
    bool running;
    pthread_t accept_thread;
    pthread_mutex_t sessions_mutex;
};

RTMPServer* rtmp_server_create(int port) {
    RTMPServer* server = calloc(1, sizeof(RTMPServer));
    if (!server) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to allocate server structure");
        return NULL;
    }

    server->port = port;
    server->running = false;
    server->num_sessions = 0;
    pthread_mutex_init(&server->sessions_mutex, NULL);

    rtmp_log(RTMP_LOG_INFO, "RTMP Server created on port %d", port);
    return server;
}

void rtmp_server_destroy(RTMPServer* server) {
    if (!server) return;

    rtmp_server_stop(server);
    pthread_mutex_destroy(&server->sessions_mutex);
    free(server);
    rtmp_log(RTMP_LOG_INFO, "RTMP Server destroyed");
}

static void set_socket_options(int socket_fd) {
    int flag = 1;
    // Disable Nagle's algorithm for lower latency
    setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    
    // Set non-blocking mode
    int flags = fcntl(socket_fd, F_GETFL, 0);
    fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
    
    // Set receive buffer size
    int rcvbuf = 262144; // 256KB
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
}

int rtmp_server_start(RTMPServer* server) {
    if (!server) return RTMP_ERROR_MEMORY;

    struct sockaddr_in addr;
    server->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->socket_fd < 0) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to create socket: %s", strerror(errno));
        return RTMP_ERROR_SOCKET;
    }

    // Importante: Permite reutilizar o endereço/porta
    int opt = 1;
    setsockopt(server->socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Configura socket para não bloquear
    int flags = fcntl(server->socket_fd, F_GETFL, 0);
    fcntl(server->socket_fd, F_SETFL, flags | O_NONBLOCK);

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(server->port);

    if (bind(server->socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to bind socket: %s", strerror(errno));
        close(server->socket_fd);
        return RTMP_ERROR_BIND;
    }

    if (listen(server->socket_fd, RTMP_MAX_CONNECTIONS) < 0) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to listen: %s", strerror(errno));
        close(server->socket_fd);
        return RTMP_ERROR_LISTEN;
    }

    server->running = true;
    rtmp_log(RTMP_LOG_INFO, "RTMP Server started successfully");
    return RTMP_OK;
}

void rtmp_server_stop(RTMPServer* server) {
    if (!server || !server->running) return;

    server->running = false;
    close(server->socket_fd);
    
    pthread_mutex_lock(&server->sessions_mutex);
    for (int i = 0; i < server->num_sessions; i++) {
        if (server->sessions[i]) {
            // TODO: Implementar rtmp_session_destroy
            server->sessions[i] = NULL;
        }
    }
    server->num_sessions = 0;
    pthread_mutex_unlock(&server->sessions_mutex);

    rtmp_log(RTMP_LOG_INFO, "RTMP Server stopped");
}