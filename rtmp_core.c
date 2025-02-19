#include "rtmp_core.h"
#include "rtmp_log.h"
#include "rtmp_net.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int server_running = 0;
static uint16_t server_port = RTMP_DEFAULT_PORT;
static uint32_t chunk_size = RTMP_MAX_CHUNK_SIZE;
static uint32_t window_size = RTMP_DEFAULT_BUFFER_SIZE;

int rtmp_init(const char* log_file) {
    rtmp_log_init(log_file);
    log_rtmp_level(RTMP_LOG_INFO, "Inicializando servidor RTMP");
    
    // Inicializar sistema de rede
    if (rtmp_net_init() < 0) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao inicializar sistema de rede");
        return -1;
    }

    return 0;
}

void rtmp_cleanup(void) {
    if (server_running) {
        rtmp_stop_server();
    }
    
    rtmp_net_cleanup();
    rtmp_log_cleanup();
}

int rtmp_start_server(uint16_t port) {
    if (server_running) {
        log_rtmp_level(RTMP_LOG_WARN, "Servidor já está rodando");
        return -1;
    }

    server_port = port;
    
    if (rtmp_net_start_server(port) < 0) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao iniciar servidor na porta %d", port);
        return -1;
    }

    server_running = 1;
    log_rtmp_level(RTMP_LOG_INFO, "Servidor RTMP iniciado na porta %d", port);
    return 0;
}

void rtmp_stop_server(void) {
    if (!server_running) return;

    rtmp_net_stop_server();
    server_running = 0;
    log_rtmp_level(RTMP_LOG_INFO, "Servidor RTMP parado");
}

int rtmp_is_running(void) {
    return server_running;
}

uint16_t rtmp_get_port(void) {
    return server_port;
}

void rtmp_set_chunk_size(uint32_t size) {
    chunk_size = size;
}

void rtmp_set_window_size(uint32_t size) {
    window_size = size;
}