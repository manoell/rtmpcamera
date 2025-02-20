// rtmp_session.c
#include "rtmp_session.h"
#include "rtmp_amf.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

static uint64_t get_current_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

RTMPSession* rtmp_session_create(int socket_fd, struct RTMPServer* server) {
    RTMPSession* session = calloc(1, sizeof(RTMPSession));
    if (!session) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to allocate session");
        return NULL;
    }

    session->socket_fd = socket_fd;
    session->state = RTMP_SESSION_CREATED;
    session->server = server;
    session->is_running = false;
    
    session->chunk_stream = rtmp_chunk_stream_create();
    if (!session->chunk_stream) {
        free(session);
        rtmp_log(RTMP_LOG_ERROR, "Failed to create chunk stream");
        return NULL;
    }

    pthread_mutex_init(&session->mutex, NULL);

    rtmp_log(RTMP_LOG_INFO, "Created new RTMP session (fd: %d)", socket_fd);
    return session;
}

void rtmp_session_destroy(RTMPSession* session) {
    if (!session) return;

    rtmp_session_stop(session);
    
    if (session->chunk_stream) {
        rtmp_chunk_stream_destroy(session->chunk_stream);
    }
    
    pthread_mutex_destroy(&session->mutex);
    
    close(session->socket_fd);
    free(session);
    
    rtmp_log(RTMP_LOG_INFO, "Destroyed RTMP session");
}

static void* session_thread(void* arg) {
    RTMPSession* session = (RTMPSession*)arg;
    
    // Realiza handshake
    if (rtmp_handshake_perform(session) != RTMP_OK) {
        rtmp_log(RTMP_LOG_ERROR, "Handshake failed");
        session->state = RTMP_SESSION_ERROR;
        return NULL;
    }
    
    session->state = RTMP_SESSION_HANDSHAKE;
    rtmp_log(RTMP_LOG_INFO, "Session handshake completed");

    // Loop principal de processamento
    while (session->is_running) {
        if (rtmp_session_process(session) != RTMP_OK) {
            break;
        }
    }

    session->state = RTMP_SESSION_CLOSED;
    return NULL;
}

int rtmp_session_start(RTMPSession* session) {
    if (!session) return RTMP_ERROR_MEMORY;
    
    session->is_running = true;
    session->stream_info.start_time = get_current_time();
    
    // Cria thread dedicada para a sessão
    if (pthread_create(&session->thread, NULL, session_thread, session) != 0) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to create session thread");
        return RTMP_ERROR_MEMORY;
    }

    rtmp_log(RTMP_LOG_INFO, "Started RTMP session");
    return RTMP_OK;
}

void rtmp_session_stop(RTMPSession* session) {
    if (!session || !session->is_running) return;

    session->is_running = false;
    pthread_join(session->thread, NULL);
    
    rtmp_log(RTMP_LOG_INFO, "Stopped RTMP session");
}

int rtmp_session_process(RTMPSession* session) {
    if (!session) return RTMP_ERROR_MEMORY;

    RTMPMessage message;
    memset(&message, 0, sizeof(message));

    int ret = rtmp_chunk_read(session->chunk_stream, &message);
    if (ret != RTMP_OK) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to read chunk");
        return ret;
    }

    // Processa a mensagem
    rtmp_session_handle_message(session, &message);
    
    // Libera recursos da mensagem
    if (message.payload) {
        free(message.payload);
    }

    return RTMP_OK;
}

void rtmp_session_handle_message(RTMPSession* session, RTMPMessage* message) {
    if (!session || !message) return;

    pthread_mutex_lock(&session->mutex);

    switch (message->message_type_id) {
        case RTMP_MSG_VIDEO:
            session->stream_info.total_frames++;
            rtmp_session_update_stream_info(session, message);
            break;

        case RTMP_MSG_COMMAND_AMF0:
            // Processa comandos AMF0
            RTMPBuffer buffer = {
                .data = message->payload,
                .size = message->message_length,
                .position = 0
            };
            
            char* command_name;
            uint16_t name_len;
            if (rtmp_amf0_read_string(&buffer, &command_name, &name_len) == RTMP_OK) {
                rtmp_log(RTMP_LOG_DEBUG, "Received command: %s", command_name);
                free(command_name);
            }
            break;
    }

    pthread_mutex_unlock(&session->mutex);
}

void rtmp_session_update_stream_info(RTMPSession* session, RTMPMessage* message) {
    if (!session || !message) return;

    uint64_t current_time = get_current_time();
    
    // Calcula FPS atual
    if (current_time > session->stream_info.start_time) {
        double elapsed_seconds = (current_time - session->stream_info.start_time) / 1000.0;
        session->stream_info.fps = session->stream_info.total_frames / elapsed_seconds;
    }

    // Calcula latência de rede
    session->stream_info.network_latency = 
        (float)(current_time - message->timestamp);

    // Log periódico das estatísticas (a cada 5 segundos)
    static uint64_t last_log = 0;
    if (current_time - last_log > 5000) {
        rtmp_session_log_stats(session);
        last_log = current_time;
    }
}

void rtmp_session_log_stats(RTMPSession* session) {
    if (!session) return;

    rtmp_log(RTMP_LOG_INFO, "Stream Statistics:");
    rtmp_log(RTMP_LOG_INFO, "  Resolution: %dx%d", 
             session->stream_info.width, 
             session->stream_info.height);
    rtmp_log(RTMP_LOG_INFO, "  FPS: %.2f", 
             session->stream_info.fps);
    rtmp_log(RTMP_LOG_INFO, "  Video Bitrate: %d kbps", 
             session->stream_info.video_bitrate / 1000);
    rtmp_log(RTMP_LOG_INFO, "  Audio Bitrate: %d kbps", 
             session->stream_info.audio_bitrate / 1000);
    rtmp_log(RTMP_LOG_INFO, "  Network Latency: %.2f ms", 
             session->stream_info.network_latency);
    rtmp_log(RTMP_LOG_INFO, "  Total Frames: %u", 
             session->stream_info.total_frames);
}