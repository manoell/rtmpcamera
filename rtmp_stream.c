#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include "rtmp_stream.h"
#include "rtmp_diagnostics.h"

// Buffer circular para frames
struct frame_buffer {
    video_frame_t *frames;
    int head;
    int tail;
    int size;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

// Estrutura principal do stream
struct rtmp_stream {
    // Configuração
    stream_config_t config;
    
    // Estado
    bool is_connected;
    bool is_running;
    uint32_t current_bitrate;
    uint32_t current_fps;
    
    // Buffer de frames
    struct frame_buffer *buffer;
    
    // Estatísticas
    stream_stats_t stats;
    uint32_t last_stats_update;
    
    // Callbacks
    frame_callback_t frame_callback;
    status_callback_t status_callback;
    void *callback_ctx;
    
    // Threads e sincronização
    pthread_t process_thread;
    pthread_mutex_t lock;
};

// Inicialização do buffer
static struct frame_buffer *frame_buffer_create(int size) {
    struct frame_buffer *buffer = calloc(1, sizeof(struct frame_buffer));
    if (!buffer) return NULL;
    
    buffer->frames = calloc(size, sizeof(video_frame_t));
    if (!buffer->frames) {
        free(buffer);
        return NULL;
    }
    
    buffer->size = size;
    pthread_mutex_init(&buffer->lock, NULL);
    pthread_cond_init(&buffer->cond, NULL);
    
    return buffer;
}

// Liberação do buffer
static void frame_buffer_destroy(struct frame_buffer *buffer) {
    if (!buffer) return;
    
    pthread_mutex_lock(&buffer->lock);
    
    for (int i = 0; i < buffer->size; i++) {
        if (buffer->frames[i].data) {
            free(buffer->frames[i].data);
        }
    }
    
    free(buffer->frames);
    
    pthread_mutex_unlock(&buffer->lock);
    pthread_mutex_destroy(&buffer->lock);
    pthread_cond_destroy(&buffer->cond);
    
    free(buffer);
}

// Thread de processamento
static void *process_thread(void *arg) {
    rtmp_stream_t *stream = (rtmp_stream_t *)arg;
    uint32_t frame_interval = 1000 / stream->config.target_fps;
    uint32_t last_frame_time = 0;
    
    while (stream->is_running) {
        uint32_t current_time = get_timestamp();
        
        // Controle de FPS
        if (current_time - last_frame_time < frame_interval) {
            usleep(1000); // 1ms
            continue;
        }
        
        pthread_mutex_lock(&stream->buffer->lock);
        
        // Verifica se há frames disponíveis
        if (stream->buffer->head != stream->buffer->tail) {
            video_frame_t *frame = &stream->buffer->frames[stream->buffer->tail];
            
            // Processa frame
            if (stream->frame_callback) {
                stream->frame_callback(frame, stream->callback_ctx);
            }
            
            // Atualiza estatísticas
            stream->stats.total_frames++;
            stream->stats.current_fps = stream->current_fps;
            stream->stats.buffer_usage = (float)(stream->buffer->head - stream->buffer->tail) / 
                                       stream->buffer->size;
            
            // Atualiza ponteiros
            stream->buffer->tail = (stream->buffer->tail + 1) % stream->buffer->size;
            last_frame_time = current_time;
            
            // Notifica status se necessário 
            if (current_time - stream->last_stats_update >= 1000) {
                if (stream->status_callback) {
                    stream->status_callback(stream->stats, stream->callback_ctx);
                }
                stream->last_stats_update = current_time;
            }
        }
        
        pthread_mutex_unlock(&stream->buffer->lock);
    }
    
    return NULL;
}

rtmp_stream_t *rtmp_stream_create(const stream_config_t *config) {
    rtmp_stream_t *stream = calloc(1, sizeof(rtmp_stream_t));
    if (!stream) return NULL;
    
    // Configura com valores padrão ou fornecidos
    if (config) {
        stream->config = *config;
    } else {
        stream->config.width = 1920;
        stream->config.height = 1080;
        stream->config.target_fps = 30;
        stream->config.initial_bitrate = 4000000; // 4 Mbps
        stream->config.buffer_size = STREAM_BUFFER_SIZE;
        stream->config.hardware_acceleration = true;
        stream->config.is_local_network = true;
    }
    
    // Inicializa buffer
    stream->buffer = frame_buffer_create(MAX_QUEUE_SIZE);
    if (!stream->buffer) {
        free(stream);
        return NULL;
    }
    
    // Inicializa mutex
    pthread_mutex_init(&stream->lock, NULL);
    
    // Configura estado inicial
    stream->current_bitrate = stream->config.initial_bitrate;
    stream->current_fps = stream->config.target_fps;
    
    rtmp_diagnostics_log(LOG_INFO, "Stream criado com sucesso - %dx%d @%dfps",
                        stream->config.width, stream->config.height, stream->config.target_fps);
    
    return stream;
}

int rtmp_stream_connect(rtmp_stream_t *stream, const char *url) {
    if (!stream || !url) return -1;
    
    pthread_mutex_lock(&stream->lock);
    
    if (stream->is_connected) {
        pthread_mutex_unlock(&stream->lock);
        return 0;
    }
    
    // Inicia thread de processamento
    stream->is_running = true;
    if (pthread_create(&stream->process_thread, NULL, process_thread, stream) != 0) {
        rtmp_diagnostics_log(LOG_ERROR, "Falha ao criar thread de processamento");
        pthread_mutex_unlock(&stream->lock);
        return -1;
    }
    
    stream->is_connected = true;
    rtmp_diagnostics_log(LOG_INFO, "Conectado ao servidor RTMP: %s", url);
    
    pthread_mutex_unlock(&stream->lock);
    return 0;
}

void rtmp_stream_disconnect(rtmp_stream_t *stream) {
    if (!stream) return;
    
    pthread_mutex_lock(&stream->lock);
    
    if (stream->is_connected) {
        stream->is_running = false;
        pthread_join(stream->process_thread, NULL);
        stream->is_connected = false;
        
        rtmp_diagnostics_log(LOG_INFO, "Desconectado do servidor RTMP");
    }
    
    pthread_mutex_unlock(&stream->lock);
}

video_frame_t *rtmp_stream_get_next_frame(rtmp_stream_t *stream) {
    if (!stream || !stream->is_connected) return NULL;
    
    pthread_mutex_lock(&stream->buffer->lock);
    
    // Aguarda frame disponível
    while (stream->buffer->head == stream->buffer->tail && stream->is_running) {
        pthread_cond_wait(&stream->buffer->cond, &stream->buffer->lock);
    }
    
    if (!stream->is_running) {
        pthread_mutex_unlock(&stream->buffer->lock);
        return NULL;
    }
    
    video_frame_t *frame = &stream->buffer->frames[stream->buffer->tail];
    stream->buffer->tail = (stream->buffer->tail + 1) % stream->buffer->size;
    
    pthread_mutex_unlock(&stream->buffer->lock);
    return frame;
}

int rtmp_stream_queue_frame(rtmp_stream_t *stream, const uint8_t *data, size_t length, uint32_t timestamp) {
    if (!stream || !data || !length) return -1;
    
    pthread_mutex_lock(&stream->buffer->lock);
    
    // Verifica overflow
    if (((stream->buffer->head + 1) % stream->buffer->size) == stream->buffer->tail) {
        stream->stats.dropped_frames++;
        pthread_mutex_unlock(&stream->buffer->lock);
        return -1;
    }
    
    // Copia frame
    video_frame_t *frame = &stream->buffer->frames[stream->buffer->head];
    frame->data = malloc(length);
    if (!frame->data) {
        pthread_mutex_unlock(&stream->buffer->lock);
        return -1;
    }
    
    memcpy(frame->data, data, length);
    frame->length = length;
    frame->timestamp = timestamp;
    frame->is_keyframe = (data[0] & 0xf0) == 0x10;
    
    stream->buffer->head = (stream->buffer->head + 1) % stream->buffer->size;
    pthread_cond_signal(&stream->buffer->cond);
    
    pthread_mutex_unlock(&stream->buffer->lock);
    return 0;
}

void rtmp_stream_set_bitrate(rtmp_stream_t *stream, uint32_t bitrate) {
    if (!stream) return;
    
    pthread_mutex_lock(&stream->lock);
    stream->current_bitrate = bitrate;
    stream->stats.target_bitrate = bitrate;
    pthread_mutex_unlock(&stream->lock);
}

stream_stats_t rtmp_stream_get_stats(rtmp_stream_t *stream) {
    stream_stats_t stats = {0};
    if (!stream) return stats;
    
    pthread_mutex_lock(&stream->lock);
    stats = stream->stats;
    pthread_mutex_unlock(&stream->lock);
    
    return stats;
}

void rtmp_stream_destroy(rtmp_stream_t *stream) {
    if (!stream) return;
    
    rtmp_stream_disconnect(stream);
    frame_buffer_destroy(stream->buffer);
    pthread_mutex_destroy(&stream->lock);
    
    free(stream);
    
    rtmp_diagnostics_log(LOG_INFO, "Stream destruído");
}