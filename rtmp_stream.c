#include "rtmp_stream.h"
#include "rtmp_log.h"
#include "rtmp_session.h"
#include <stdlib.h>
#include <string.h>

int rtmp_create_stream_id(rtmp_session_t* session) {
    if (!session || session->stream_count >= RTMP_MAX_STREAMS) {
        log_rtmp_level(RTMP_LOG_ERROR, "Limite de streams atingido");
        return -1;
    }
    
    int stream_id = session->stream_count + 1;
    rtmp_stream_t* stream = &session->streams[session->stream_count];
    
    stream->id = stream_id;
    stream->active = 1;
    stream->type = 0;
    stream->data = NULL;
    stream->data_size = 0;
    stream->timestamp = 0;
    
    session->stream_count++;
    
    log_rtmp_level(RTMP_LOG_INFO, "Novo stream criado: %d", stream_id);
    return stream_id;
}

rtmp_stream_t* rtmp_get_stream(rtmp_session_t* session, uint32_t stream_id) {
    if (!session || stream_id == 0) return NULL;
    
    for (int i = 0; i < session->stream_count; i++) {
        if (session->streams[i].id == stream_id) {
            return &session->streams[i];
        }
    }
    
    return NULL;
}

void rtmp_delete_stream(rtmp_session_t* session, uint32_t stream_id) {
    if (!session) return;
    
    for (int i = 0; i < session->stream_count; i++) {
        if (session->streams[i].id == stream_id) {
            session->streams[i].active = 0;
            if (session->streams[i].data) {
                free(session->streams[i].data);
                session->streams[i].data = NULL;
            }
            log_rtmp_level(RTMP_LOG_INFO, "Stream deletado: %d", stream_id);
            break;
        }
    }
}

int rtmp_process_video(rtmp_session_t* session, rtmp_packet_t* packet) {
    if (!session || !packet || !packet->data) return -1;
    
    rtmp_stream_t* stream = rtmp_get_stream(session, packet->stream_id);
    if (!stream) {
        log_rtmp_level(RTMP_LOG_ERROR, "Stream não encontrado: %d", packet->stream_id);
        return -1;
    }

    // Atualizar timestamp
    stream->timestamp = packet->timestamp;

    // Se preview estiver habilitado, atualizar
    if (session->preview_enabled) {
        rtmp_session_update_preview(session, packet->data, packet->data_size);
    }

    // Processar frame de vídeo
    if (stream->active) {
        rtmp_stream_buffer_data(stream, packet->data, packet->data_size);
    }

    log_rtmp_level(RTMP_LOG_DEBUG, "Frame de vídeo processado: %d bytes", packet->data_size);
    return 0;
}

int rtmp_process_audio(rtmp_session_t* session, rtmp_packet_t* packet) {
    if (!session || !packet || !packet->data) return -1;
    
    rtmp_stream_t* stream = rtmp_get_stream(session, packet->stream_id);
    if (!stream) {
        log_rtmp_level(RTMP_LOG_ERROR, "Stream não encontrado: %d", packet->stream_id);
        return -1;
    }

    // Atualizar timestamp
    stream->timestamp = packet->timestamp;

    // Processar frame de áudio
    if (stream->active) {
        rtmp_stream_buffer_data(stream, packet->data, packet->data_size);
    }

    log_rtmp_level(RTMP_LOG_DEBUG, "Frame de áudio processado: %d bytes", packet->data_size);
    return 0;
}

int rtmp_stream_buffer_data(rtmp_stream_t* stream, uint8_t* data, uint32_t size) {
    if (!stream || !data || size == 0) return -1;

    // Realocar buffer se necessário
    void* new_data = realloc(stream->data, stream->data_size + size);
    if (!new_data) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao realocar buffer do stream");
        return -1;
    }

    stream->data = new_data;
    memcpy((uint8_t*)stream->data + stream->data_size, data, size);
    stream->data_size += size;

    return 0;
}

void rtmp_stream_clear_buffer(rtmp_stream_t* stream) {
    if (!stream) return;
    
    if (stream->data) {
        free(stream->data);
        stream->data = NULL;
    }
    stream->data_size = 0;
}

int rtmp_stream_start(rtmp_stream_t* stream) {
    if (!stream) return -1;
    stream->active = 1;
    return 0;
}

int rtmp_stream_stop(rtmp_stream_t* stream) {
    if (!stream) return -1;
    stream->active = 0;
    rtmp_stream_clear_buffer(stream);
    return 0;
}

int rtmp_stream_pause(rtmp_stream_t* stream) {
    if (!stream) return -1;
    stream->active = 0;
    return 0;
}

int rtmp_stream_resume(rtmp_stream_t* stream) {
    if (!stream) return -1;
    stream->active = 1;
    return 0;
}

int rtmp_stream_is_active(rtmp_stream_t* stream) {
    if (!stream) return 0;
    return stream->active;
}

uint32_t rtmp_stream_get_timestamp(rtmp_stream_t* stream) {
    if (!stream) return 0;
    return stream->timestamp;
}