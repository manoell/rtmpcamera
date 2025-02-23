#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "rtmp_session.h"
#include "rtmp_core.h"
#include "rtmp_chunk.h"
#include "rtmp_handshake.h"
#include "rtmp_amf.h"
#include "rtmp_utils.h"

#define SESSION_BUFFER_SIZE (1024 * 1024)  // 1MB buffer
#define SESSION_MAX_RETRIES 5
#define SESSION_RETRY_DELAY 1000  // 1 segundo
#define SESSION_PING_INTERVAL 5000  // 5 segundos

struct rtmp_session {
    rtmp_session_config_t config;
    rtmp_session_stats_t stats;
    rtmp_connection_t *conn;
    rtmp_chunk_context_t *chunk_ctx;
    
    int state;
    int is_running;
    pthread_t worker_thread;
    pthread_mutex_t mutex;
    
    uint8_t *send_buffer;
    uint8_t *recv_buffer;
    
    uint64_t last_ping_time;
    uint64_t last_ping_response;
    uint32_t transaction_id;
    
    rtmp_session_state_callback_t state_callback;
    rtmp_session_error_callback_t error_callback;
    void *user_data;
};

static void session_set_state(rtmp_session_t *session, int new_state) {
    pthread_mutex_lock(&session->mutex);
    
    int old_state = session->state;
    session->state = new_state;
    
    if (new_state == RTMP_SESSION_STATE_CONNECTED) {
        session->stats.connect_time = rtmp_get_time_ms();
    } else if (new_state == RTMP_SESSION_STATE_PUBLISHING) {
        session->stats.publish_time = rtmp_get_time_ms();
    }
    
    if (session->state_callback && old_state != new_state) {
        session->state_callback(session->user_data, old_state, new_state);
    }
    
    pthread_mutex_unlock(&session->mutex);
}

static void session_handle_error(rtmp_session_t *session, const char *error) {
    session_set_state(session, RTMP_SESSION_STATE_ERROR);
    
    if (session->error_callback) {
        session->error_callback(session->user_data, error);
    }
}

static int session_send_connect(rtmp_session_t *session) {
    uint8_t *buffer = session->send_buffer;
    size_t size;
    
    // Prepara comando connect
    if (!rtmp_amf_encode_connect(session->config.app_name,
                                "", // swf_url
                                "", // tc_url
                                buffer,
                                &size)) {
        return 0;
    }
    
    // Envia comando
    rtmp_chunk_t chunk = {0};
    chunk.chunk_type = RTMP_CHUNK_TYPE_0;
    chunk.chunk_stream_id = RTMP_CHUNK_STREAM_INVOKE;
    chunk.header.message_type = RTMP_MSG_COMMAND_AMF0;
    chunk.header.message_length = size;
    chunk.header.timestamp = 0;
    chunk.header.message_stream_id = 0;
    chunk.data = buffer;
    chunk.data_size = size;
    
    if (!rtmp_chunk_encode(session->chunk_ctx, &chunk, buffer, &size)) {
        return 0;
    }
    
    return rtmp_send(session->conn, buffer, size);
}

static int session_send_publish(rtmp_session_t *session) {
    uint8_t *buffer = session->send_buffer;
    size_t size;
    
    // Prepara comando publish
    if (!rtmp_amf_encode_publish(session->config.stream_name,
                                buffer,
                                &size)) {
        return 0;
    }
    
    // Envia comando
    rtmp_chunk_t chunk = {0};
    chunk.chunk_type = RTMP_CHUNK_TYPE_0;
    chunk.chunk_stream_id = RTMP_CHUNK_STREAM_INVOKE;
    chunk.header.message_type = RTMP_MSG_COMMAND_AMF0;
    chunk.header.message_length = size;
    chunk.header.timestamp = 0;
    chunk.header.message_stream_id = 1;
    chunk.data = buffer;
    chunk.data_size = size;
    
    if (!rtmp_chunk_encode(session->chunk_ctx, &chunk, buffer, &size)) {
        return 0;
    }
    
    return rtmp_send(session->conn, buffer, size);
}

static void* session_worker_thread(void *arg) {
    rtmp_session_t *session = (rtmp_session_t*)arg;
    uint8_t *buffer = session->recv_buffer;
    size_t buffer_size = SESSION_BUFFER_SIZE;
    int ret;
    
    while (session->is_running) {
        pthread_mutex_lock(&session->mutex);
        
        // Verifica necessidade de ping
        uint64_t now = rtmp_get_time_ms();
        if (now - session->last_ping_time >= SESSION_PING_INTERVAL) {
            // Envia ping
            rtmp_send_ping(session->conn);
            session->last_ping_time = now;
            session->stats.ping_count++;
        }
        
        // Recebe dados
        ret = rtmp_recv(session->conn, buffer, buffer_size);
        if (ret > 0) {
            session->stats.bytes_received += ret;
            session->stats.messages_received++;
            
            // Processa chunk
            rtmp_chunk_t chunk = {0};
            size_t bytes_read;
            
            if (rtmp_chunk_decode(session->chunk_ctx, buffer, ret, &chunk, &bytes_read)) {
                // Processa mensagem baseado no tipo
                switch (chunk.header.message_type) {
                    case RTMP_MSG_COMMAND_AMF0:
                        // Processa comandos AMF0
                        // TODO: Implementar processamento de comandos
                        break;
                        
                    case RTMP_MSG_USER_CONTROL:
                        // Processa mensagens de controle
                        if (chunk.data[0] == 0x07) {  // Ping Response
                            session->last_ping_response = now;
                            session->stats.rtt = now - session->last_ping_time;
                        }
                        break;
                }
                
                if (chunk.data) {
                    free(chunk.data);
                }
            }
        } else if (ret == 0) {
            // Conexão fechada
            session_set_state(session, RTMP_SESSION_STATE_CLOSED);
            break;
        } else {
            if (rtmp_would_block()) {
                pthread_mutex_unlock(&session->mutex);
                usleep(1000);  // 1ms
                continue;
            }
            // Erro
            session_handle_error(session, "Connection error");
            break;
        }
        
        pthread_mutex_unlock(&session->mutex);
    }
    
    return NULL;
}

rtmp_session_t* rtmp_session_create(const rtmp_session_config_t *config) {
    rtmp_session_t *session = calloc(1, sizeof(rtmp_session_t));
    if (!session) return NULL;
    
    // Copia configuração
    memcpy(&session->config, config, sizeof(rtmp_session_config_t));
    
    // Inicializa conexão
    rtmp_config_t conn_config = {0};
    session->conn = rtmp_create(&conn_config);
    if (!session->conn) {
        free(session);
        return NULL;
    }
    
    // Inicializa contexto de chunks
    session->chunk_ctx = rtmp_chunk_context_create();
    if (!session->chunk_ctx) {
        rtmp_destroy(session->conn);
        free(session);
        return NULL;
    }
    
    // Aloca buffers
    session->send_buffer = malloc(SESSION_BUFFER_SIZE);
    session->recv_buffer = malloc(SESSION_BUFFER_SIZE);
    if (!session->send_buffer || !session->recv_buffer) {
        rtmp_chunk_context_destroy(session->chunk_ctx);
        rtmp_destroy(session->conn);
        free(session->send_buffer);
        free(session->recv_buffer);
        free(session);
        return NULL;
    }
    
    pthread_mutex_init(&session->mutex, NULL);
    session->state = RTMP_SESSION_STATE_INIT;
    
    return session;
}

void rtmp_session_destroy(rtmp_session_t *session) {
    if (!session) return;
    
    rtmp_session_disconnect(session);
    
    pthread_mutex_destroy(&session->mutex);
    rtmp_chunk_context_destroy(session->chunk_ctx);
    rtmp_destroy(session->conn);
    free(session->send_buffer);
    free(session->recv_buffer);
    free(session);
}

int rtmp_session_connect(rtmp_session_t *session) {
    if (!session || session->state != RTMP_SESSION_STATE_INIT) return 0;
    
    session_set_state(session, RTMP_SESSION_STATE_CONNECTING);
    
    // Conecta
    if (!rtmp_connect(session->conn)) {
        session_handle_error(session, "Connection failed");
        return 0;
    }
    
    // Handshake
    if (!rtmp_handshake_client(session->conn)) {
        session_handle_error(session, "Handshake failed");
        return 0;
    }
    
    // Envia comando connect
    if (!session_send_connect(session)) {
        session_handle_error(session, "Connect command failed");
        return 0;
    }
    
    // Inicia thread de trabalho
    session->is_running = 1;
    if (pthread_create(&session->worker_thread, NULL, session_worker_thread, session) != 0) {
        session_handle_error(session, "Failed to create worker thread");
        return 0;
    }
    
    return 1;
}

int rtmp_session_disconnect(rtmp_session_t *session) {
    if (!session) return 0;
    
    session->is_running = 0;
    
    if (session->worker_thread) {
        pthread_join(session->worker_thread, NULL);
        session->worker_thread = 0;
    }
    
    rtmp_disconnect(session->conn);
    session_set_state(session, RTMP_SESSION_STATE_CLOSED);
    
    return 1;
}

int rtmp_session_start_publish(rtmp_session_t *session) {
    if (!session || session->state != RTMP_SESSION_STATE_CONNECTED) return 0;
    
    if (!session_send_publish(session)) {
        session_handle_error(session, "Publish command failed");
        return 0;
    }
    
    session_set_state(session, RTMP_SESSION_STATE_PUBLISHING);
    return 1;
}

int rtmp_session_stop_publish(rtmp_session_t *session) {
    if (!session || session->state != RTMP_SESSION_STATE_PUBLISHING) return 0;
    
    // TODO: Implementar comando unpublish
    
    session_set_state(session, RTMP_SESSION_STATE_CONNECTED);
    return 1;
}

int rtmp_session_send_video(rtmp_session_t *session, const uint8_t *data, size_t size, int64_t timestamp) {
    if (!session || !data || session->state != RTMP_SESSION_STATE_PUBLISHING) return 0;
    
    pthread_mutex_lock(&session->mutex);
    
    rtmp_chunk_t chunk = {0};
    chunk.chunk_type = RTMP_CHUNK_TYPE_0;
    chunk.chunk_stream_id = RTMP_CHUNK_STREAM_VIDEO;
    chunk.header.message_type = RTMP_MSG_VIDEO;
    chunk.header.message_length = size;
    chunk.header.timestamp = timestamp;
    chunk.header.message_stream_id = 1;
    chunk.data = (uint8_t*)data;
    chunk.data_size = size;
    
    size_t encoded_size;
    if (!rtmp_chunk_encode(session->chunk_ctx, &chunk, session->send_buffer, &encoded_size)) {
        pthread_mutex_unlock(&session->mutex);
        return 0;
    }
    
    int result = rtmp_send(session->conn, session->send_buffer, encoded_size);
    if (result > 0) {
        session->stats.bytes_sent += result;
        session->stats.messages_sent++;
    }
    
    pthread_mutex_unlock(&session->mutex);
    return result > 0;
}

int rtmp_session_send_audio(rtmp_session_t *session, const uint8_t *data, size_t size, int64_t timestamp) {
    if (!session || !data || session->state != RTMP_SESSION_STATE_PUBLISHING) return 0;
    
    pthread_mutex_lock(&session->mutex);
    
    rtmp_chunk_t chunk = {0};
    chunk.chunk_type = RTMP_CHUNK_TYPE_0;
    chunk.chunk_stream_id = RTMP_CHUNK_STREAM_AUDIO;
    chunk.header.message_type = RTMP_MSG_AUDIO;
    chunk.header.message_length = size;
    chunk.header.timestamp = timestamp;
    chunk.header.message_stream_id = 1;
    chunk.data = (uint8_t*)data;
    chunk.data_size = size;
    
    size_t encoded_size;
    if (!rtmp_chunk_encode(session->chunk_ctx, &chunk, session->send_buffer, &encoded_size)) {
        pthread_mutex_unlock(&session->mutex);
        return 0;
    }
    
    int result = rtmp_send(session->conn, session->send_buffer, encoded_size);
    if (result > 0) {
        session->stats.bytes_sent += result;
        session->stats.messages_sent++;
    }
    
    pthread_mutex_unlock(&session->mutex);
    return result > 0;
}

int rtmp_session_send_metadata(rtmp_session_t *session, const char *name, const uint8_t *data, size_t size) {
    if (!session || !name || !data || session->state != RTMP_SESSION_STATE_PUBLISHING) return 0;
    
    pthread_mutex_lock(&session->mutex);
    
    size_t metadata_size;
    if (!rtmp_amf_encode_metadata(name, data, size, session->send_buffer, &metadata_size)) {
        pthread_mutex_unlock(&session->mutex);
        return 0;
    }
    
    rtmp_chunk_t chunk = {0};
    chunk.chunk_type = RTMP_CHUNK_TYPE_0;
    chunk.chunk_stream_id = RTMP_CHUNK_STREAM_DATA;
    chunk.header.message_type = RTMP_MSG_DATA_AMF0;
    chunk.header.message_length = metadata_size;
    chunk.header.timestamp = rtmp_get_time_ms();
    chunk.header.message_stream_id = 1;
    chunk.data = session->send_buffer;
    chunk.data_size = metadata_size;
    
    size_t encoded_size;
    if (!rtmp_chunk_encode(session->chunk_ctx, &chunk, session->send_buffer, &encoded_size)) {
        pthread_mutex_unlock(&session->mutex);
        return 0;
    }
    
    int result = rtmp_send(session->conn, session->send_buffer, encoded_size);
    if (result > 0) {
        session->stats.bytes_sent += result;
        session->stats.messages_sent++;
    }
    
    pthread_mutex_unlock(&session->mutex);
    return result > 0;
}

int rtmp_session_get_stats(rtmp_session_t *session, rtmp_session_stats_t *stats) {
    if (!session || !stats) return 0;
    
    pthread_mutex_lock(&session->mutex);
    
    // Calcula métricas em tempo real
    uint64_t now = rtmp_get_time_ms();
    uint64_t elapsed = now - session->stats.connect_time;
    
    if (elapsed > 0) {
        session->stats.bandwidth_in = (float)(session->stats.bytes_received * 8) / elapsed;
        session->stats.bandwidth_out = (float)(session->stats.bytes_sent * 8) / elapsed;
    }
    
    // Copia estatísticas
    memcpy(stats, &session->stats, sizeof(rtmp_session_stats_t));
    
    pthread_mutex_unlock(&session->mutex);
    return 1;
}

int rtmp_session_get_state(rtmp_session_t *session) {
    if (!session) return RTMP_SESSION_STATE_ERROR;
    
    pthread_mutex_lock(&session->mutex);
    int state = session->state;
    pthread_mutex_unlock(&session->mutex);
    
    return state;
}

void rtmp_session_set_state_callback(rtmp_session_t *session, rtmp_session_state_callback_t callback) {
    if (!session) return;
    
    pthread_mutex_lock(&session->mutex);
    session->state_callback = callback;
    pthread_mutex_unlock(&session->mutex);
}

void rtmp_session_set_error_callback(rtmp_session_t *session, rtmp_session_error_callback_t callback) {
    if (!session) return;
    
    pthread_mutex_lock(&session->mutex);
    session->error_callback = callback;
    pthread_mutex_unlock(&session->mutex);
}