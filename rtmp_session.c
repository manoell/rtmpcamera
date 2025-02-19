#include "rtmp_session.h"
#include "rtmp_log.h"
#include "rtmp_handshake.h"
#include "rtmp_net.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

rtmp_session_t* rtmp_create_session(int socket, struct sockaddr_in addr) {
    rtmp_session_t* session = (rtmp_session_t*)calloc(1, sizeof(rtmp_session_t));
    if (!session) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao alocar memória para sessão");
        return NULL;
    }

    session->socket = socket;
    session->addr = addr;
    session->state = RTMP_STATE_INIT;
    session->connected = 1;
    session->in_chunk_size = RTMP_MAX_CHUNK_SIZE;
    session->out_chunk_size = RTMP_MAX_CHUNK_SIZE;
    session->window_size = RTMP_DEFAULT_BUFFER_SIZE;
    session->last_ack = 0;  // Inicializar last_ack

    // Alocar buffers
    session->in_buffer = (uint8_t*)malloc(session->window_size);
    session->out_buffer = (uint8_t*)malloc(session->window_size);
    
    if (!session->in_buffer || !session->out_buffer) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao alocar buffers da sessão");
        rtmp_destroy_session(session);
        return NULL;
    }

    session->in_buffer_size = 0;
    session->out_buffer_size = 0;
    session->stream_count = 0;
    session->preview_enabled = 0;
    session->preview_data = NULL;

    log_rtmp_level(RTMP_LOG_INFO, "Nova sessão criada (socket: %d)", socket);
    return session;
}

void rtmp_destroy_session(rtmp_session_t* session) {
    if (!session) return;

    if (session->socket >= 0) {
        close(session->socket);
    }

    free(session->in_buffer);
    free(session->out_buffer);
    
    // Limpar streams
    for (int i = 0; i < session->stream_count; i++) {
        if (session->streams[i].data) {
            free(session->streams[i].data);
        }
    }
    
    // Limpar preview
    if (session->preview_data) {
        free(session->preview_data);
    }
    
    free(session);
    log_rtmp_level(RTMP_LOG_INFO, "Sessão destruída");
}

int rtmp_session_handle(rtmp_session_t* session) {
    if (!session) return -1;

    switch (session->state) {
        case RTMP_STATE_INIT:
            return rtmp_handshake_init(session);
            
        case RTMP_STATE_HANDSHAKE_C0C1:
        case RTMP_STATE_HANDSHAKE_C2:
            // Processamento continuará no loop principal
            return 0;
            
        case RTMP_STATE_CONNECTED:
        case RTMP_STATE_STREAMING:
            return rtmp_maintain_connection(session);
            
        case RTMP_STATE_ERROR:
            return -1;
            
        default:
            log_rtmp_level(RTMP_LOG_ERROR, "Estado de sessão inválido");
            return -1;
    }
}

int rtmp_session_buffer_data(rtmp_session_t* session, uint8_t* data, uint32_t size) {
    if (!session || !data || size == 0) return -1;
    
    if (session->in_buffer_size + size > session->window_size) {
        log_rtmp_level(RTMP_LOG_ERROR, "Buffer de entrada cheio");
        return -1;
    }
    
    memcpy(session->in_buffer + session->in_buffer_size, data, size);
    session->in_buffer_size += size;
    
    return 0;
}

void rtmp_session_clear_buffers(rtmp_session_t* session) {
    if (!session) return;
    
    session->in_buffer_size = 0;
    session->out_buffer_size = 0;
}

int rtmp_session_is_connected(rtmp_session_t* session) {
    if (!session) return 0;
    return session->connected;
}

rtmp_state_t rtmp_session_get_state(rtmp_session_t* session) {
    if (!session) return RTMP_STATE_ERROR;
    return session->state;
}

// Funções de preview
int rtmp_session_enable_preview(rtmp_session_t* session) {
    if (!session) return -1;
    session->preview_enabled = 1;
    return 0;
}

int rtmp_session_disable_preview(rtmp_session_t* session) {
    if (!session) return -1;
    session->preview_enabled = 0;
    if (session->preview_data) {
        free(session->preview_data);
        session->preview_data = NULL;
    }
    return 0;
}

int rtmp_session_update_preview(rtmp_session_t* session, void* data, uint32_t size) {
    if (!session || !session->preview_enabled || !data || size == 0) return -1;
    
    void* new_data = malloc(size);
    if (!new_data) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao alocar memória para preview");
        return -1;
    }
    
    memcpy(new_data, data, size);
    
    if (session->preview_data) {
        free(session->preview_data);
    }
    
    session->preview_data = new_data;
    return 0;
}