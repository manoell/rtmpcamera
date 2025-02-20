// rtmp_core.h
#ifndef RTMP_CORE_H
#define RTMP_CORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>

#define RTMP_LOG_FILE "/var/tmp/rtmp_debug.log"
#define RTMP_MAX_CONNECTIONS 10
#define RTMP_DEFAULT_CHUNK_SIZE 128
#define RTMP_DEFAULT_BUFFER_SIZE 4096

// Códigos de retorno
#define RTMP_OK 0
#define RTMP_ERROR_SOCKET -1
#define RTMP_ERROR_BIND -2
#define RTMP_ERROR_LISTEN -3
#define RTMP_ERROR_HANDSHAKE -4
#define RTMP_ERROR_MEMORY -5

// Log levels
typedef enum {
    RTMP_LOG_DEBUG,
    RTMP_LOG_INFO,
    RTMP_LOG_WARNING,
    RTMP_LOG_ERROR
} RTMPLogLevel;

// Estruturas principais
typedef struct RTMPServer RTMPServer;
typedef struct RTMPSession RTMPSession;

// Funções de log
void rtmp_log(RTMPLogLevel level, const char* format, ...);
RTMPServer* rtmp_server_create(int port);
void rtmp_server_destroy(RTMPServer* server);
int rtmp_server_start(RTMPServer* server);
void rtmp_server_stop(RTMPServer* server);

#endif