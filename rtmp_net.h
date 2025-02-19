#ifndef RTMP_NET_H
#define RTMP_NET_H

#include "rtmp_types.h"
#include <stdint.h>

// Funções de inicialização
int rtmp_net_init(void);
void rtmp_net_cleanup(void);

// Funções do servidor
int rtmp_net_start_server(uint16_t port);
void rtmp_net_stop_server(void);

// Funções de conexão
int rtmp_net_accept_client(void);
void rtmp_net_disconnect_client(rtmp_session_t* session);

// Funções de E/S
int rtmp_net_read(rtmp_session_t* session, uint8_t* buffer, uint32_t size);
int rtmp_net_write(rtmp_session_t* session, const uint8_t* data, uint32_t size);

// Funções auxiliares
int rtmp_net_set_nonblocking(int socket);
int rtmp_net_set_timeout(int socket, int seconds);

// Funções de manutenção de conexão
int rtmp_maintain_connection(rtmp_session_t* session);

#endif // RTMP_NET_H