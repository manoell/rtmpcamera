#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include "rtmp_core.h"
#include "rtmp_diagnostics.h"

// Buffer de leitura/escrita otimizado
#define IO_BUFFER_SIZE (256 * 1024)  // 256KB

// Estrutura de buffer circular
typedef struct {
    uint8_t *data;
    size_t size;
    size_t read_pos;
    size_t write_pos;
} circular_buffer_t;

// Contexto de IO
typedef struct {
    circular_buffer_t *read_buffer;
    circular_buffer_t *write_buffer;
    pthread_mutex_t read_mutex;
    pthread_mutex_t write_mutex;
} io_context_t;

// Funções auxiliares
static circular_buffer_t *create_circular_buffer(size_t size) {
    circular_buffer_t *buffer = calloc(1, sizeof(circular_buffer_t));
    if (!buffer) return NULL;
    
    buffer->data = malloc(size);
    if (!buffer->data) {
        free(buffer);
        return NULL;
    }
    
    buffer->size = size;
    return buffer;
}

static void destroy_circular_buffer(circular_buffer_t *buffer) {
    if (buffer) {
        free(buffer->data);
        free(buffer);
    }
}

static io_context_t *create_io_context(void) {
    io_context_t *ctx = calloc(1, sizeof(io_context_t));
    if (!ctx) return NULL;
    
    ctx->read_buffer = create_circular_buffer(IO_BUFFER_SIZE);
    ctx->write_buffer = create_circular_buffer(IO_BUFFER_SIZE);
    
    if (!ctx->read_buffer || !ctx->write_buffer) {
        destroy_circular_buffer(ctx->read_buffer);
        destroy_circular_buffer(ctx->write_buffer);
        free(ctx);
        return NULL;
    }
    
    pthread_mutex_init(&ctx->read_mutex, NULL);
    pthread_mutex_init(&ctx->write_mutex, NULL);
    
    return ctx;
}

static void destroy_io_context(io_context_t *ctx) {
    if (ctx) {
        destroy_circular_buffer(ctx->read_buffer);
        destroy_circular_buffer(ctx->write_buffer);
        pthread_mutex_destroy(&ctx->read_mutex);
        pthread_mutex_destroy(&ctx->write_mutex);
        free(ctx);
    }
}

rtmp_session_t *rtmp_session_create(void) {
    rtmp_session_t *session = calloc(1, sizeof(rtmp_session_t));
    if (!session) return NULL;
    
    session->connection = calloc(1, sizeof(rtmp_connection_t));
    if (!session->connection) {
        free(session);
        return NULL;
    }
    
    // Configura valores padrão
    session->connection->chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
    session->connection->window_size = 2500000;
    session->connection->bandwidth = 2500000;
    session->buffer_length = RTMP_BUFFER_TIME;
    
    rtmp_diagnostics_log(LOG_INFO, "Sessão RTMP criada");
    return session;
}

int rtmp_connect(rtmp_session_t *session, const char *url) {
    if (!session || !url) return -1;
    
    // Parse URL
    char *url_copy = strdup(url);
    char *host = strstr(url_copy, "://");
    if (!host) {
        free(url_copy);
        return -1;
    }
    
    host += 3;
    char *port_str = strchr(host, ':');
    char *path = strchr(host, '/');
    
    if (port_str) *port_str++ = '\0';
    if (path) *path++ = '\0';
    
    int port = port_str ? atoi(port_str) : RTMP_DEFAULT_PORT;
    
    // Cria socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        free(url_copy);
        return -1;
    }
    
    // Configura socket para performance
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));
    
    // Buffer sizes otimizados
    int rcvbuf = 256 * 1024;  // 256KB
    int sndbuf = 256 * 1024;  // 256KB
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    
    // Conecta
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr(host);
    
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(sock);
        free(url_copy);
        return -1;
    }
    
    session->connection->socket = sock;
    session->connection->is_connected = true;
    
    // Configura contexto de IO
    session->connection->user_data = create_io_context();
    
    // Salva informações da URL
    session->app_name = path ? strdup(path) : strdup("");
    session->tcp_url = strdup(url);
    
    free(url_copy);
    
    rtmp_diagnostics_log(LOG_INFO, "Conectado a %s:%d", host, port);
    return 0;
}

int rtmp_read_packet(rtmp_session_t *session, rtmp_packet_t *packet) {
    if (!session || !packet || !session->connection->is_connected) return -1;
    
    io_context_t *io = session->connection->user_data;
    pthread_mutex_lock(&io->read_mutex);
    
    // Lê cabeçalho básico
    uint8_t header[1];
    if (read(session->connection->socket, header, 1) != 1) {
        pthread_mutex_unlock(&io->read_mutex);
        return -1;
    }
    
    packet->channel_id = header[0] & 0x3f;
    int header_type = header[0] >> 6;
    
    // Lê resto do cabeçalho baseado no tipo
    switch (header_type) {
        case 0: // 12 bytes
            // Implementação do header tipo 0
            break;
        case 1: // 8 bytes
            // Implementação do header tipo 1
            break;
        case 2: // 4 bytes
            // Implementação do header tipo 2
            break;
        case 3: // 1 byte
            // Implementação do header tipo 3
            break;
    }
    
    // Lê dados do pacote
    if (packet->length > 0) {
        packet->data = malloc(packet->length);
        if (!packet->data) {
            pthread_mutex_unlock(&io->read_mutex);
            return -1;
        }
        
        size_t bytes_read = 0;
        while (bytes_read < packet->length) {
            ssize_t result = read(session->connection->socket,
                                packet->data + bytes_read,
                                packet->length - bytes_read);
            if (result <= 0) {
                free(packet->data);
                pthread_mutex_unlock(&io->read_mutex);
                return -1;
            }
            bytes_read += result;
        }
    }
    
    pthread_mutex_unlock(&io->read_mutex);
    return 0;
}

int rtmp_write_packet(rtmp_session_t *session, rtmp_packet_t *packet) {
    if (!session || !packet || !session->connection->is_connected) return -1;
    
    io_context_t *io = session->connection->user_data;
    pthread_mutex_lock(&io->write_mutex);
    
    // Escreve cabeçalho
    uint8_t header[12] = {0};
    header[0] = (packet->channel_id & 0x3f) | (0 << 6);  // Tipo 0
    
    // Timestamp
    header[1] = (packet->timestamp >> 16) & 0xff;
    header[2] = (packet->timestamp >> 8) & 0xff;
    header[3] = packet->timestamp & 0xff;
    
    // Length
    header[4] = (packet->length >> 16) & 0xff;
    header[5] = (packet->length >> 8) & 0xff;
    header[6] = packet->length & 0xff;
    
    // Type
    header[7] = packet->type;
    
    // Stream ID
    header[8] = packet->stream_id & 0xff;
    header[9] = (packet->stream_id >> 8) & 0xff;
    header[10] = (packet->stream_id >> 16) & 0xff;
    header[11] = (packet->stream_id >> 24) & 0xff;
    
    // Escreve cabeçalho
    if (write(session->connection->socket, header, 12) != 12) {
        pthread_mutex_unlock(&io->write_mutex);
        return -1;
    }
    
    // Escreve dados
    if (packet->length > 0 && packet->data) {
        size_t bytes_written = 0;
        while (bytes_written < packet->length) {
            ssize_t result = write(session->connection->socket,
                                 packet->data + bytes_written,
                                 packet->length - bytes_written);
            if (result <= 0) {
                pthread_mutex_unlock(&io->write_mutex);
                return -1;
            }
            bytes_written += result;
        }
    }
    
    pthread_mutex_unlock(&io->write_mutex);
    return 0;
}

void rtmp_disconnect(rtmp_session_t *session) {
    if (!session || !session->connection) return;
    
    if (session->connection->is_connected) {
        close(session->connection->socket);
        session->connection->is_connected = false;
        
        io_context_t *io = session->connection->user_data;
        if (io) {
            destroy_io_context(io);
            session->connection->user_data = NULL;
        }
        
        rtmp_diagnostics_log(LOG_INFO, "Desconectado do servidor RTMP");
    }
}

void rtmp_session_destroy(rtmp_session_t *session) {
    if (!session) return;
    
    rtmp_disconnect(session);
    
    free(session->app_name);
    free(session->stream_name);
    free(session->swf_url);
    free(session->tcp_url);
    free(session->connection);
    free(session);
    
    rtmp_diagnostics_log(LOG_INFO, "Sessão RTMP destruída");
}

void rtmp_set_chunk_size(rtmp_session_t *session, uint32_t size) {
    if (!session || !session->connection) return;
    
    session->connection->chunk_size = size;
    
    // Envia comando de chunk size
    rtmp_packet_t packet = {0};
    packet.channel_id = 2;
    packet.type = RTMP_PACKET_TYPE_CHUNK_SIZE;
    packet.length = 4;
    packet.stream_id = 0;
    
    uint8_t data[4];
    data[0] = (size >> 24) & 0xff;
    data[1] = (size >> 16) & 0xff;
    data[2] = (size >> 8) & 0xff;
    data[3] = size & 0xff;
    
    packet.data = data;
    rtmp_write_packet(session, &packet);
}

void rtmp_set_buffer_length(rtmp_session_t *session, uint32_t length) {
    if (!session) return;
    session->buffer_length = length;
}

void rtmp_set_window_size(rtmp_session_t *session, uint32_t size) {
    if (!session || !session->connection) return;
    
    session->connection->window_size = size;
    
    // Envia comando de window size
    rtmp_packet_t packet = {0};
    packet.channel_id = 2;
    packet.type = RTMP_PACKET_TYPE_SERVER_BW;
    packet.length = 4;
    packet.stream_id = 0;
    
    uint8_t data[4];
    data[0] = (size >> 24) & 0xff;
    data[1] = (size >> 16) & 0xff;
    data[2] = (size >> 8) & 0xff;
    data[3] = size & 0xff;
    
    packet.data = data;
    rtmp_write_packet(session, &packet);
}

void rtmp_set_bandwidth(rtmp_session_t *session, uint32_t bandwidth) {
    if (!session || !session->connection) return;
    
    session->connection->bandwidth = bandwidth;
    
    // Envia comando de bandwidth
    rtmp_packet_t packet = {0};
    packet.channel_id = 2;
    packet.type = RTMP_PACKET_TYPE_CLIENT_BW;
    packet.length = 5;
    packet.stream_id = 0;
    
    uint8_t data[5];
    data[0] = (bandwidth >> 24) & 0xff;
    data[1] = (bandwidth >> 16) & 0xff;
    data[2] = (bandwidth >> 8) & 0xff;
    data[3] = bandwidth & 0xff;
    data[4] = 2; // Dynamic bandwidth
    
    packet.data = data;
    rtmp_write_packet(session, &packet);
}

bool rtmp_is_connected(rtmp_session_t *session) {
    return session && session->connection && session->connection->is_connected;
}

uint32_t rtmp_get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

static bool debug_enabled = false;

void rtmp_set_debug(bool enable) {
    debug_enabled = enable;
}

void rtmp_dump_packet(rtmp_packet_t *packet) {
    if (!debug_enabled || !packet) return;
    
    rtmp_diagnostics_log(LOG_DEBUG, "RTMP Packet:");
    rtmp_diagnostics_log(LOG_DEBUG, "  Channel ID: %d", packet->channel_id);
    rtmp_diagnostics_log(LOG_DEBUG, "  Timestamp: %u", packet->timestamp);
    rtmp_diagnostics_log(LOG_DEBUG, "  Length: %u", packet->length);
    rtmp_diagnostics_log(LOG_DEBUG, "  Type: %d", packet->type);
    rtmp_diagnostics_log(LOG_DEBUG, "  Stream ID: %u", packet->stream_id);
}

const char *rtmp_get_error_string(int error) {
    switch (error) {
        case -1: return "Erro genérico";
        case -2: return "Timeout de conexão";
        case -3: return "Handshake falhou";
        case -4: return "Erro de leitura/escrita";
        case -5: return "Buffer cheio";
        default: return "Erro desconhecido";
    }
}

// Funções utilitárias adicionais
static int read_fully(int socket, uint8_t *buffer, size_t size) {
    size_t total_read = 0;
    
    while (total_read < size) {
        ssize_t result = read(socket, buffer + total_read, size - total_read);
        if (result <= 0) return -1;
        total_read += result;
    }
    
    return total_read;
}

static int write_fully(int socket, const uint8_t *buffer, size_t size) {
    size_t total_written = 0;
    
    while (total_written < size) {
        ssize_t result = write(socket, buffer + total_written, size - total_written);
        if (result <= 0) return -1;
        total_written += result;
    }
    
    return total_written;
}

// Funções de socket não bloqueante
static int set_nonblocking(int socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(socket, F_SETFL, flags | O_NONBLOCK);
}

static int wait_for_socket(int socket, bool for_read, int timeout_ms) {
    fd_set fds;
    struct timeval tv;
    
    FD_ZERO(&fds);
    FD_SET(socket, &fds);
    
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    
    return select(socket + 1, 
                 for_read ? &fds : NULL,
                 for_read ? NULL : &fds,
                 NULL, &tv);
}

// Manipulação de timestamps
static uint32_t get_relative_timestamp(rtmp_session_t *session) {
    uint32_t current = rtmp_get_time();
    if (!session->connection->timestamp) {
        session->connection->timestamp = current;
    }
    return current - session->connection->timestamp;
}

// Gerenciamento de sequência
static uint32_t get_next_sequence(rtmp_session_t *session) {
    return ++session->connection->sequence_number;
}