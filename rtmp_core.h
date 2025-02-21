#ifndef RTMP_CORE_H
#define RTMP_CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdbool.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif

// Definições de log
#define RTMP_LOG_FILE "/var/tmp/rtmp_debug.log"
#define RTMP_MAX_CONNECTIONS 10
#define RTMP_BUFFER_SIZE 65536

typedef enum {
    RTMP_LOG_DEBUG,
    RTMP_LOG_INFO,
    RTMP_LOG_WARNING,
    RTMP_LOG_ERROR
} RTMPLogLevel;

// Estados do cliente RTMP
#define RTMP_STATE_HANDSHAKE_S0S1 0
#define RTMP_STATE_HANDSHAKE_S2   1
#define RTMP_STATE_CONNECT        2
#define RTMP_STATE_READY          3

// Estrutura do cliente
typedef struct RTMPClient {
    int socket_fd;
    struct sockaddr_in addr;
    uint8_t *in_buffer;
    uint8_t *out_buffer;
    size_t in_buffer_size;
    size_t out_buffer_size;
    int state;
    pthread_t thread;
    bool running;
    void *user_data;
} RTMPClient;

// Estrutura do servidor
typedef struct RTMPServer {
    int socket_fd;
    bool running;
    pthread_t accept_thread;
    struct sockaddr_in addr;
    RTMPClient *clients[RTMP_MAX_CONNECTIONS];
    int client_count;
    pthread_mutex_t clients_mutex;
} RTMPServer;

// Códigos de retorno
#define RTMP_OK 0
#define RTMP_ERROR_SOCKET -1
#define RTMP_ERROR_BIND -2
#define RTMP_ERROR_LISTEN -3
#define RTMP_ERROR_HANDSHAKE -4
#define RTMP_ERROR_MEMORY -5
#define RTMP_ERROR_PROTOCOL -6

// Funções públicas
void rtmp_log(RTMPLogLevel level, const char* format, ...);
RTMPServer* rtmp_server_create(int port);
void rtmp_server_destroy(RTMPServer* server);
int rtmp_server_start(RTMPServer* server);
void rtmp_server_stop(RTMPServer* server);

// Funções de handshake
int rtmp_handshake_handle(RTMPClient *client);

// Funções de leitura/escrita com timeout
ssize_t read_with_timeout(int fd, void *buf, size_t count, int timeout_ms);
ssize_t write_with_timeout(int fd, const void *buf, size_t count, int timeout_ms);

// Função de processamento de pacotes
int rtmp_handle_packet(RTMPClient *client, uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif