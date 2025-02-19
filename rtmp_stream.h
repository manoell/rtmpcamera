#ifndef RTMP_STREAM_H
#define RTMP_STREAM_H

#include "rtmp_types.h"

// Funções de gerenciamento de stream
int rtmp_create_stream_id(rtmp_session_t* session);
rtmp_stream_t* rtmp_get_stream(rtmp_session_t* session, uint32_t stream_id);
void rtmp_delete_stream(rtmp_session_t* session, uint32_t stream_id);

// Funções de processamento de mídia
int rtmp_process_video(rtmp_session_t* session, rtmp_packet_t* packet);
int rtmp_process_audio(rtmp_session_t* session, rtmp_packet_t* packet);

// Funções de buffer de stream
int rtmp_stream_buffer_data(rtmp_stream_t* stream, uint8_t* data, uint32_t size);
void rtmp_stream_clear_buffer(rtmp_stream_t* stream);

// Funções de controle de stream
int rtmp_stream_start(rtmp_stream_t* stream);
int rtmp_stream_stop(rtmp_stream_t* stream);
int rtmp_stream_pause(rtmp_stream_t* stream);
int rtmp_stream_resume(rtmp_stream_t* stream);

// Funções de estado
int rtmp_stream_is_active(rtmp_stream_t* stream);
uint32_t rtmp_stream_get_timestamp(rtmp_stream_t* stream);

#endif // RTMP_STREAM_H