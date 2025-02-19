#ifndef RTMP_CORE_H
#define RTMP_CORE_H

#include "rtmp_types.h"

// Funções de inicialização e limpeza
int rtmp_init(const char* log_file);
void rtmp_cleanup(void);

// Funções de gerenciamento do servidor
int rtmp_start_server(uint16_t port);
void rtmp_stop_server(void);

// Estado do servidor
int rtmp_is_running(void);
uint16_t rtmp_get_port(void);

// Configurações
void rtmp_set_chunk_size(uint32_t size);
void rtmp_set_window_size(uint32_t size);

#endif // RTMP_CORE_H