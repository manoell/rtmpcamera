#ifndef RTMP_SESSION_H
#define RTMP_SESSION_H

#include "rtmp_types.h"

// Funções de gerenciamento de sessão
rtmp_session_t* rtmp_create_session(int socket, struct sockaddr_in addr);
void rtmp_destroy_session(rtmp_session_t* session);
int rtmp_session_handle(rtmp_session_t* session);

// Funções de buffer
int rtmp_session_buffer_data(rtmp_session_t* session, uint8_t* data, uint32_t size);
void rtmp_session_clear_buffers(rtmp_session_t* session);

// Funções de estado
int rtmp_session_is_connected(rtmp_session_t* session);
rtmp_state_t rtmp_session_get_state(rtmp_session_t* session);

// Funções de preview
int rtmp_session_enable_preview(rtmp_session_t* session);
int rtmp_session_disable_preview(rtmp_session_t* session);
int rtmp_session_update_preview(rtmp_session_t* session, void* data, uint32_t size);

#endif // RTMP_SESSION_H