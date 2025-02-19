#include "rtmp_net.h"
#include "rtmp_log.h"
#include "rtmp_packet.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static int server_socket = -1;

int rtmp_net_init(void) {
    server_socket = -1;
    return 0;
}

void rtmp_net_cleanup(void) {
    if (server_socket >= 0) {
        close(server_socket);
        server_socket = -1;
    }
}

int rtmp_net_start_server(uint16_t port) {
    struct sockaddr_in addr;
    int yes = 1;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao criar socket: %s", strerror(errno));
        return -1;
    }

    // Permitir reuso do endereço
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao configurar socket: %s", strerror(errno));
        close(server_socket);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao fazer bind: %s", strerror(errno));
        close(server_socket);
        return -1;
    }

    if (listen(server_socket, 5) < 0) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao fazer listen: %s", strerror(errno));
        close(server_socket);
        return -1;
    }

    rtmp_net_set_nonblocking(server_socket);
    log_rtmp_level(RTMP_LOG_INFO, "Servidor iniciado na porta %d", port);
    return 0;
}

void rtmp_net_stop_server(void) {
    if (server_socket >= 0) {
        close(server_socket);
        server_socket = -1;
    }
}

int rtmp_net_accept_client(void) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &addr_len);
    if (client_socket < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_rtmp_level(RTMP_LOG_ERROR, "Falha ao aceitar cliente: %s", strerror(errno));
        }
        return -1;
    }

    rtmp_net_set_nonblocking(client_socket);
    rtmp_net_set_timeout(client_socket, 30);

    log_rtmp_level(RTMP_LOG_INFO, "Cliente conectado: %s:%d", 
        inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

    return client_socket;
}

void rtmp_net_disconnect_client(rtmp_session_t* session) {
    if (!session) return;
    
    if (session->socket >= 0) {
        close(session->socket);
        session->socket = -1;
    }
    
    session->connected = 0;
    log_rtmp_level(RTMP_LOG_INFO, "Cliente desconectado");
}

int rtmp_net_read(rtmp_session_t* session, uint8_t* buffer, uint32_t size) {
    if (!session || !buffer || size == 0) return -1;

    int bytes_read = recv(session->socket, buffer, size, 0);
    if (bytes_read < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log_rtmp_level(RTMP_LOG_ERROR, "Erro na leitura: %s", strerror(errno));
            return -1;
        }
        return 0;
    } else if (bytes_read == 0) {
        log_rtmp_level(RTMP_LOG_INFO, "Conexão fechada pelo cliente");
        return -1;
    }

    session->bytes_in += bytes_read;
    return bytes_read;
}

int rtmp_net_write(rtmp_session_t* session, const uint8_t* data, uint32_t size) {
    if (!session || !data || size == 0) return -1;

    int bytes_written = send(session->socket, data, size, 0);
    if (bytes_written < 0) {
        log_rtmp_level(RTMP_LOG_ERROR, "Erro na escrita: %s", strerror(errno));
        return -1;
    }

    session->bytes_out += bytes_written;
    return bytes_written;
}

int rtmp_net_set_nonblocking(int socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(socket, F_SETFL, flags | O_NONBLOCK);
}

int rtmp_net_set_timeout(int socket, int seconds) {
    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    return setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

int rtmp_maintain_connection(rtmp_session_t* session) {
    if (!session || !session->connected) return -1;

    // Enviar ping a cada 30 segundos para manter a conexão viva
    static time_t last_ping_time = 0;
    time_t current_time = time(NULL);
    
    if (current_time - last_ping_time > 30) {
        rtmp_send_ping(session);
        last_ping_time = current_time;
        log_rtmp_level(RTMP_LOG_DEBUG, "Ping enviado para manter conexão");
    }

    // Verificar se precisamos enviar ACK
    if (session->bytes_in - session->last_ack > session->window_size / 2) {
        rtmp_send_ack(session);
        session->last_ack = session->bytes_in;
    }

    return 0;
}