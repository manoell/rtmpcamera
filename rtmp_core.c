#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include "rtmp_core.h"
#include "rtmp_handshake.h"
#include "rtmp_chunk.h"
#include "rtmp_amf.h"
#include "rtmp_utils.h"

#define RTMP_SOCKET_BUFFER_SIZE (256 * 1024)
#define RTMP_MAX_QUEUE_SIZE 1000
#define RTMP_PING_INTERVAL 5000

typedef struct rtmp_message {
    uint8_t *data;
    size_t size;
    rtmp_message_type_t type;
    uint32_t timestamp;
    uint32_t stream_id;
    struct rtmp_message *next;
} rtmp_message_t;

typedef struct rtmp_queue {
    rtmp_message_t *head;
    rtmp_message_t *tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} rtmp_queue_t;

struct rtmp_connection {
    rtmp_config_t config;
    int socket;
    rtmp_state_t state;
    pthread_t thread;
    int thread_running;
    
    rtmp_queue_t send_queue;
    rtmp_queue_t receive_queue;
    
    uint32_t chunk_size;
    uint32_t window_size;
    uint32_t buffer_time;
    uint32_t stream_id;
    uint32_t transaction_id;
    
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t last_ping_time;
    
    rtmp_state_callback_t state_callback;
    rtmp_error_callback_t error_callback;
    void *user_data;
    
    pthread_mutex_t state_mutex;
    pthread_mutex_t socket_mutex;
};

static void rtmp_queue_init(rtmp_queue_t *queue) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
}

static void rtmp_queue_destroy(rtmp_queue_t *queue) {
    pthread_mutex_lock(&queue->mutex);
    
    rtmp_message_t *msg = queue->head;
    while (msg) {
        rtmp_message_t *next = msg->next;
        free(msg->data);
        free(msg);
        msg = next;
    }
    
    pthread_mutex_unlock(&queue->mutex);
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

static int rtmp_queue_push(rtmp_queue_t *queue, rtmp_message_t *msg) {
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->count >= RTMP_MAX_QUEUE_SIZE) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }
    
    msg->next = NULL;
    if (queue->tail) {
        queue->tail->next = msg;
    } else {
        queue->head = msg;
    }
    queue->tail = msg;
    queue->count++;
    
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    return 1;
}

static rtmp_message_t* rtmp_queue_pop(rtmp_queue_t *queue) {
    pthread_mutex_lock(&queue->mutex);
    
    while (queue->count == 0) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }
    
    rtmp_message_t *msg = queue->head;
    queue->head = msg->next;
    if (!queue->head) {
        queue->tail = NULL;
    }
    queue->count--;
    
    pthread_mutex_unlock(&queue->mutex);
    return msg;
}

static void rtmp_set_state(rtmp_connection_t *conn, rtmp_state_t new_state) {
    pthread_mutex_lock(&conn->state_mutex);
    rtmp_state_t old_state = conn->state;
    conn->state = new_state;
    
    if (conn->state_callback && old_state != new_state) {
        conn->state_callback(conn->user_data, old_state, new_state);
    }
    
    pthread_mutex_unlock(&conn->state_mutex);
}

static void rtmp_handle_error(rtmp_connection_t *conn, const char *error) {
    rtmp_set_state(conn, RTMP_STATE_ERROR);
    
    if (conn->error_callback) {
        conn->error_callback(conn->user_data, error);
    }
}

static int rtmp_socket_connect(rtmp_connection_t *conn) {
    struct sockaddr_in addr;
    int ret;
    
    conn->socket = socket(AF_INET, SOCK_STREAM, 0);
    if (conn->socket < 0) {
        rtmp_handle_error(conn, "Failed to create socket");
        return 0;
    }
    
    // Set non-blocking
    int flags = fcntl(conn->socket, F_GETFL, 0);
    fcntl(conn->socket, F_SETFL, flags | O_NONBLOCK);
    
    // Set TCP options
    int opt = 1;
    setsockopt(conn->socket, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    
    // Set buffer sizes
    opt = RTMP_SOCKET_BUFFER_SIZE;
    setsockopt(conn->socket, SOL_SOCKET, SO_SNDBUF, &opt, sizeof(opt));
    setsockopt(conn->socket, SOL_SOCKET, SO_RCVBUF, &opt, sizeof(opt));
    
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(conn->config.port);
    addr.sin_addr.s_addr = inet_addr(conn->config.host);
    
    ret = connect(conn->socket, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) {
        rtmp_handle_error(conn, "Failed to connect");
        return 0;
    }
    
    return 1;
}

static void* rtmp_thread_func(void *arg) {
    rtmp_connection_t *conn = (rtmp_connection_t*)arg;
    rtmp_message_t *msg;
    fd_set read_fds, write_fds;
    struct timeval tv;
    int max_fd, ret;
    
    while (conn->thread_running) {
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        FD_SET(conn->socket, &read_fds);
        
        if (conn->send_queue.count > 0) {
            FD_SET(conn->socket, &write_fds);
        }
        
        max_fd = conn->socket;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms
        
        ret = select(max_fd + 1, &read_fds, &write_fds, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            rtmp_handle_error(conn, "Select error");
            break;
        }
        
        // Handle write
        if (FD_ISSET(conn->socket, &write_fds)) {
            msg = rtmp_queue_pop(&conn->send_queue);
            if (msg) {
                pthread_mutex_lock(&conn->socket_mutex);
                ret = send(conn->socket, msg->data, msg->size, 0);
                pthread_mutex_unlock(&conn->socket_mutex);
                
                if (ret < 0) {
                    rtmp_handle_error(conn, "Send error");
                    free(msg->data);
                    free(msg);
                    break;
                }
                
                conn->bytes_sent += ret;
                conn->messages_sent++;
                
                free(msg->data);
                free(msg);
            }
        }
        
        // Handle read
        if (FD_ISSET(conn->socket, &read_fds)) {
            uint8_t buffer[4096];
            
            pthread_mutex_lock(&conn->socket_mutex);
            ret = recv(conn->socket, buffer, sizeof(buffer), 0);
            pthread_mutex_unlock(&conn->socket_mutex);
            
            if (ret <= 0) {
                if (ret == 0 || errno != EAGAIN) {
                    rtmp_handle_error(conn, "Connection closed");
                    break;
                }
            } else {
                conn->bytes_received += ret;
                conn->messages_received++;
                
                // Process received data
                // TODO: Implement RTMP protocol parsing
            }
        }
        
        // Handle ping
        uint64_t now = rtmp_get_time_ms();
        if (now - conn->last_ping_time >= RTMP_PING_INTERVAL) {
            // Send ping
            // TODO: Implement ping
            conn->last_ping_time = now;
        }
    }
    
    return NULL;
}

rtmp_connection_t* rtmp_create(const rtmp_config_t *config) {
    rtmp_connection_t *conn = (rtmp_connection_t*)calloc(1, sizeof(rtmp_connection_t));
    if (!conn) return NULL;
    
    memcpy(&conn->config, config, sizeof(rtmp_config_t));
    conn->socket = -1;
    conn->state = RTMP_STATE_DISCONNECTED;
    conn->chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
    conn->window_size = RTMP_DEFAULT_WINDOW_SIZE;
    conn->buffer_time = RTMP_DEFAULT_BUFFER_TIME;
    conn->stream_id = 1;
    conn->transaction_id = 1;
    
    rtmp_queue_init(&conn->send_queue);
    rtmp_queue_init(&conn->receive_queue);
    
    pthread_mutex_init(&conn->state_mutex, NULL);
    pthread_mutex_init(&conn->socket_mutex, NULL);
    
    return conn;
}

void rtmp_destroy(rtmp_connection_t *conn) {
    if (!conn) return;
    
    rtmp_disconnect(conn);
    
    rtmp_queue_destroy(&conn->send_queue);
    rtmp_queue_destroy(&conn->receive_queue);
    
    pthread_mutex_destroy(&conn->state_mutex);
    pthread_mutex_destroy(&conn->socket_mutex);
    
    free(conn);
}

int rtmp_connect(rtmp_connection_t *conn) {
    if (!conn || conn->state != RTMP_STATE_DISCONNECTED) {
        return 0;
    }
    
    rtmp_set_state(conn, RTMP_STATE_CONNECTING);
    
    if (!rtmp_socket_connect(conn)) {
        return 0;
    }
    
    conn->thread_running = 1;
    if (pthread_create(&conn->thread, NULL, rtmp_thread_func, conn) != 0) {
        rtmp_handle_error(conn, "Failed to create thread");
        close(conn->socket);
        conn->socket = -1;
        conn->thread_running = 0;
        return 0;
    }
    
    // Iniciar handshake RTMP
    if (!rtmp_handshake_client(conn)) {
        rtmp_handle_error(conn, "Handshake failed");
        rtmp_disconnect(conn);
        return 0;
    }
    
    rtmp_set_state(conn, RTMP_STATE_CONNECTED);
    return 1;
}

void rtmp_disconnect(rtmp_connection_t *conn) {
    if (!conn) return;
    
    conn->thread_running = 0;
    
    if (conn->thread) {
        pthread_join(conn->thread, NULL);
        conn->thread = 0;
    }
    
    if (conn->socket >= 0) {
        close(conn->socket);
        conn->socket = -1;
    }
    
    rtmp_set_state(conn, RTMP_STATE_DISCONNECTED);
}

int rtmp_is_connected(rtmp_connection_t *conn) {
    return conn && conn->state == RTMP_STATE_CONNECTED;
}

int rtmp_publish_start(rtmp_connection_t *conn) {
    if (!conn || conn->state != RTMP_STATE_CONNECTED) {
        return 0;
    }
    
    // Enviar comando publish
    rtmp_message_t *msg = (rtmp_message_t*)calloc(1, sizeof(rtmp_message_t));
    if (!msg) return 0;
    
    // Construir mensagem AMF para publish
    uint8_t *amf_data;
    size_t amf_size;
    
    if (!rtmp_amf_encode_publish(conn->config.stream_key, &amf_data, &amf_size)) {
        free(msg);
        return 0;
    }
    
    msg->data = amf_data;
    msg->size = amf_size;
    msg->type = RTMP_MSG_COMMAND_AMF0;
    msg->timestamp = rtmp_get_time_ms();
    msg->stream_id = conn->stream_id;
    
    if (!rtmp_queue_push(&conn->send_queue, msg)) {
        free(msg->data);
        free(msg);
        return 0;
    }
    
    rtmp_set_state(conn, RTMP_STATE_PUBLISHING);
    return 1;
}

int rtmp_publish_stop(rtmp_connection_t *conn) {
    if (!conn || conn->state != RTMP_STATE_PUBLISHING) {
        return 0;
    }
    
    // Enviar comando unpublish
    rtmp_message_t *msg = (rtmp_message_t*)calloc(1, sizeof(rtmp_message_t));
    if (!msg) return 0;
    
    uint8_t *amf_data;
    size_t amf_size;
    
    if (!rtmp_amf_encode_unpublish(&amf_data, &amf_size)) {
        free(msg);
        return 0;
    }
    
    msg->data = amf_data;
    msg->size = amf_size;
    msg->type = RTMP_MSG_COMMAND_AMF0;
    msg->timestamp = rtmp_get_time_ms();
    msg->stream_id = conn->stream_id;
    
    if (!rtmp_queue_push(&conn->send_queue, msg)) {
        free(msg->data);
        free(msg);
        return 0;
    }
    
    rtmp_set_state(conn, RTMP_STATE_CONNECTED);
    return 1;
}

int rtmp_send_video(rtmp_connection_t *conn, const uint8_t *data, size_t size, int64_t timestamp) {
    if (!conn || conn->state != RTMP_STATE_PUBLISHING || !data || !size) {
        return 0;
    }
    
    rtmp_message_t *msg = (rtmp_message_t*)calloc(1, sizeof(rtmp_message_t));
    if (!msg) return 0;
    
    msg->data = (uint8_t*)malloc(size);
    if (!msg->data) {
        free(msg);
        return 0;
    }
    
    memcpy(msg->data, data, size);
    msg->size = size;
    msg->type = RTMP_MSG_VIDEO;
    msg->timestamp = timestamp;
    msg->stream_id = conn->stream_id;
    
    if (!rtmp_queue_push(&conn->send_queue, msg)) {
        free(msg->data);
        free(msg);
        return 0;
    }
    
    return 1;
}

int rtmp_send_audio(rtmp_connection_t *conn, const uint8_t *data, size_t size, int64_t timestamp) {
    if (!conn || conn->state != RTMP_STATE_PUBLISHING || !data || !size) {
        return 0;
    }
    
    rtmp_message_t *msg = (rtmp_message_t*)calloc(1, sizeof(rtmp_message_t));
    if (!msg) return 0;
    
    msg->data = (uint8_t*)malloc(size);
    if (!msg->data) {
        free(msg);
        return 0;
    }
    
    memcpy(msg->data, data, size);
    msg->size = size;
    msg->type = RTMP_MSG_AUDIO;
    msg->timestamp = timestamp;
    msg->stream_id = conn->stream_id;
    
    if (!rtmp_queue_push(&conn->send_queue, msg)) {
        free(msg->data);
        free(msg);
        return 0;
    }
    
    return 1;
}

int rtmp_send_metadata(rtmp_connection_t *conn, const char *name, const uint8_t *data, size_t size) {
    if (!conn || !name || !data || !size) {
        return 0;
    }
    
    rtmp_message_t *msg = (rtmp_message_t*)calloc(1, sizeof(rtmp_message_t));
    if (!msg) return 0;
    
    uint8_t *amf_data;
    size_t amf_size;
    
    if (!rtmp_amf_encode_metadata(name, data, size, &amf_data, &amf_size)) {
        free(msg);
        return 0;
    }
    
    msg->data = amf_data;
    msg->size = amf_size;
    msg->type = RTMP_MSG_DATA_AMF0;
    msg->timestamp = rtmp_get_time_ms();
    msg->stream_id = conn->stream_id;
    
    if (!rtmp_queue_push(&conn->send_queue, msg)) {
        free(msg->data);
        free(msg);
        return 0;
    }
    
    return 1;
}

void rtmp_set_state_callback(rtmp_connection_t *conn, rtmp_state_callback_t callback) {
    if (!conn) return;
    conn->state_callback = callback;
}

void rtmp_set_error_callback(rtmp_connection_t *conn, rtmp_error_callback_t callback) {
    if (!conn) return;
    conn->error_callback = callback;
}

int rtmp_set_chunk_size(rtmp_connection_t *conn, int size) {
    if (!conn || size < 128 || size > RTMP_MAX_CHUNK_SIZE) {
        return 0;
    }
    conn->chunk_size = size;
    return 1;
}

int rtmp_set_window_size(rtmp_connection_t *conn, int size) {
    if (!conn || size <= 0) {
        return 0;
    }
    conn->window_size = size;
    return 1;
}

int rtmp_set_buffer_time(rtmp_connection_t *conn, int time_ms) {
    if (!conn || time_ms <= 0) {
        return 0;
    }
    conn->buffer_time = time_ms;
    return 1;
}

int rtmp_get_stats(rtmp_connection_t *conn, rtmp_stats_t *stats) {
    if (!conn || !stats) {
        return 0;
    }
    
    pthread_mutex_lock(&conn->state_mutex);
    
    stats->bytes_sent = conn->bytes_sent;
    stats->bytes_received = conn->bytes_received;
    stats->messages_sent = conn->messages_sent;
    stats->messages_received = conn->messages_received;
    stats->current_chunk_size = conn->chunk_size;
    stats->current_window_size = conn->window_size;
    stats->current_buffer_time = conn->buffer_time;
    stats->state = conn->state;
    
    // Calcular bandwidth
    uint64_t now = rtmp_get_time_ms();
    uint64_t elapsed = now - conn->last_ping_time;
    if (elapsed > 0) {
        stats->bandwidth_in = (float)(conn->bytes_received * 8) / elapsed;
        stats->bandwidth_out = (float)(conn->bytes_sent * 8) / elapsed;
    }
    
    pthread_mutex_unlock(&conn->state_mutex);
    return 1;
}