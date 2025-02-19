#ifndef RTMP_PACKET_H
#define RTMP_PACKET_H

#include "rtmp_types.h"

// Funções de processamento de pacotes
int rtmp_packet_parse(rtmp_session_t* session, uint8_t* data, uint32_t size, rtmp_packet_t* packet);
int rtmp_packet_serialize(rtmp_packet_t* packet, uint8_t* buffer, uint32_t buffer_size);
int rtmp_packet_process(rtmp_session_t* session, rtmp_packet_t* packet);

// Funções de criação e destruição de pacotes
rtmp_packet_t* rtmp_packet_create(void);
void rtmp_packet_destroy(rtmp_packet_t* packet);

// Funções de envio
int rtmp_packet_send(rtmp_session_t* session, rtmp_packet_t* packet);
int rtmp_send_control_packet(rtmp_session_t* session, uint8_t type, uint32_t value);

// Funções auxiliares
int rtmp_send_ack(rtmp_session_t* session);
int rtmp_send_ping(rtmp_session_t* session);
int rtmp_send_chunk_size(rtmp_session_t* session, uint32_t chunk_size);

#endif // RTMP_PACKET_H