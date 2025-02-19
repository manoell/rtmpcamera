#ifndef RTMP_HANDSHAKE_H
#define RTMP_HANDSHAKE_H

#include "rtmp_types.h"

// Funções de handshake
int rtmp_handshake_init(rtmp_session_t* session);
int rtmp_process_handshake_c0c1(rtmp_session_t* session, uint8_t* data, uint32_t size);
int rtmp_process_handshake_c2(rtmp_session_t* session, uint8_t* data, uint32_t size);
int rtmp_send_handshake_s0s1s2(rtmp_session_t* session, uint8_t* c1_data);

#endif // RTMP_HANDSHAKE_H