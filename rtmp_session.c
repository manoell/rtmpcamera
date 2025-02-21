// rtmp_session.c
#include "rtmp_core.h"
#include "rtmp_session.h"
#include "rtmp_protocol.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

static uint64_t get_current_time() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

static void* session_thread(void* arg) {
    RTMPSession* session = (RTMPSession*)arg;
    rtmp_log(RTMP_LOG_INFO, "Session thread started");
    return NULL;
}

RTMPSession* rtmp_session_create(int socket_fd, struct RTMPServer* server) {
    RTMPSession* session = calloc(1, sizeof(RTMPSession));
    if (!session) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to allocate session");
        return NULL;
    }

    session->socket_fd = socket_fd;
    session->server = server;
    session->state = RTMP_SESSION_CREATED;
    session->is_running = false;
    
    session->chunk_stream = rtmp_chunk_stream_create(socket_fd);
    if (!session->chunk_stream) {
        free(session);
        rtmp_log(RTMP_LOG_ERROR, "Failed to create chunk stream");
        return NULL;
    }

    if (pthread_mutex_init(&session->mutex, NULL) != 0) {
        rtmp_chunk_stream_destroy(session->chunk_stream);
        free(session);
        rtmp_log(RTMP_LOG_ERROR, "Failed to init mutex");
        return NULL;
    }

    // Inicializar stream info
    memset(&session->stream_info, 0, sizeof(RTMPStreamInfo));
    session->stream_info.start_time = get_current_time();

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
    
    free(session);
    rtmp_log(RTMP_LOG_INFO, "Session destroyed");
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

    // Processar mensagem
    rtmp_protocol_handle_message(session, &message);
    
    // Liberar recursos da mensagem
    rtmp_message_free(&message);

    return RTMP_OK;
}

void rtmp_session_handle_connect(RTMPSession* session) {
    if (!session) return;

    // Processar handshake
    int ret = rtmp_handshake_perform(session);
    if (ret != RTMP_OK) {
        rtmp_log(RTMP_LOG_ERROR, "Handshake failed");
        session->state = RTMP_SESSION_ERROR;
        return;
    }

    session->state = RTMP_SESSION_HANDSHAKE;
    rtmp_log(RTMP_LOG_INFO, "Session handshake completed");

    // Loop principal
    while (session->is_running) {
        if (rtmp_session_process(session) != RTMP_OK) {
            break;
        }
    }

    session->state = RTMP_SESSION_CLOSED;
}

int rtmp_session_start(RTMPSession* session) {
    if (!session) return RTMP_ERROR_MEMORY;
    
    session->is_running = true;
    session->stream_info.start_time = get_current_time();
    
    if (pthread_create(&session->thread, NULL, session_thread, session) != 0) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to create session thread");
        return RTMP_ERROR_MEMORY;
    }

    rtmp_log(RTMP_LOG_INFO, "Session started");
    return RTMP_OK;
}

void rtmp_session_stop(RTMPSession* session) {
    if (!session || !session->is_running) return;

    session->is_running = false;
    pthread_join(session->thread, NULL);
    
    rtmp_log(RTMP_LOG_INFO, "Session stopped");
}

void rtmp_session_update_stats(RTMPSession* session) {
    if (!session) return;

    pthread_mutex_lock(&session->mutex);

    uint64_t current_time = get_current_time();
    if (current_time > session->stream_info.start_time) {
        double elapsed = (current_time - session->stream_info.start_time) / 1000.0;
        session->stream_info.fps = session->stream_info.total_frames / elapsed;
    }

    pthread_mutex_unlock(&session->mutex);
}