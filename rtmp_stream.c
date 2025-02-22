#include "rtmp_stream.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define STREAM_BUFFER_SIZE 1024 * 1024 // 1MB
#define MAX_FRAME_QUEUE_SIZE 60 // 2 segundos @ 30fps
#define MIN_FRAME_INTERVAL 1000 / 60 // 60fps max

typedef struct {
    uint8_t* data;
    size_t length;
    uint64_t timestamp;
    int type; // 1 = video, 2 = audio
} StreamFrame;

typedef struct {
    StreamFrame* frames[MAX_FRAME_QUEUE_SIZE];
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} FrameQueue;

typedef struct {
    int running;
    pthread_t thread;
    FrameQueue videoQueue;
    FrameQueue audioQueue;
    uint64_t lastVideoTimestamp;
    uint64_t lastAudioTimestamp;
    int videoFrameDropped;
    int audioFrameDropped;
    RTMPStreamStats stats;
    void* userData;
    StreamCallback callback;
} StreamProcessor;

static StreamProcessor* processor = NULL;

// Inicialização da fila de frames
static void frame_queue_init(FrameQueue* queue) {
    memset(queue, 0, sizeof(FrameQueue));
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
}

// Destruição da fila de frames
static void frame_queue_destroy(FrameQueue* queue) {
    pthread_mutex_lock(&queue->mutex);
    for (int i = 0; i < MAX_FRAME_QUEUE_SIZE; i++) {
        if (queue->frames[i]) {
            free(queue->frames[i]->data);
            free(queue->frames[i]);
        }
    }
    pthread_mutex_unlock(&queue->mutex);
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

// Adiciona frame à fila
static int frame_queue_push(FrameQueue* queue, StreamFrame* frame) {
    int dropped = 0;
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->count >= MAX_FRAME_QUEUE_SIZE) {
        // Política de dropping: descarta frame mais antigo
        StreamFrame* oldFrame = queue->frames[queue->tail];
        queue->tail = (queue->tail + 1) % MAX_FRAME_QUEUE_SIZE;
        queue->count--;
        
        if (oldFrame) {
            free(oldFrame->data);
            free(oldFrame);
        }
        dropped = 1;
    }
    
    queue->frames[queue->head] = frame;
    queue->head = (queue->head + 1) % MAX_FRAME_QUEUE_SIZE;
    queue->count++;
    
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    
    return dropped;
}

// Thread de processamento
static void* process_stream(void* arg) {
    StreamProcessor* proc = (StreamProcessor*)arg;
    uint8_t* buffer = malloc(STREAM_BUFFER_SIZE);
    
    while (proc->running) {
        // Processa vídeo
        pthread_mutex_lock(&proc->videoQueue.mutex);
        while (proc->videoQueue.count > 0) {
            StreamFrame* frame = proc->videoQueue.frames[proc->videoQueue.tail];
            proc->videoQueue.frames[proc->videoQueue.tail] = NULL;
            proc->videoQueue.tail = (proc->videoQueue.tail + 1) % MAX_FRAME_QUEUE_SIZE;
            proc->videoQueue.count--;
            
            if (frame) {
                // Verifica intervalo mínimo entre frames
                uint64_t delta = frame->timestamp - proc->lastVideoTimestamp;
                if (delta >= MIN_FRAME_INTERVAL) {
                    proc->callback(frame->data, frame->length, frame->timestamp, 1, proc->userData);
                    proc->lastVideoTimestamp = frame->timestamp;
                    proc->stats.videoFramesProcessed++;
                } else {
                    proc->videoFrameDropped++;
                    proc->stats.videoFramesDropped++;
                }
                
                free(frame->data);
                free(frame);
            }
        }
        pthread_mutex_unlock(&proc->videoQueue.mutex);
        
        // Processa áudio
        pthread_mutex_lock(&proc->audioQueue.mutex);
        while (proc->audioQueue.count > 0) {
            StreamFrame* frame = proc->audioQueue.frames[proc->audioQueue.tail];
            proc->audioQueue.frames[proc->audioQueue.tail] = NULL;
            proc->audioQueue.tail = (proc->audioQueue.tail + 1) % MAX_FRAME_QUEUE_SIZE;
            proc->audioQueue.count--;
            
            if (frame) {
                proc->callback(frame->data, frame->length, frame->timestamp, 2, proc->userData);
                proc->lastAudioTimestamp = frame->timestamp;
                proc->stats.audioFramesProcessed++;
                
                free(frame->data);
                free(frame);
            }
        }
        pthread_mutex_unlock(&proc->audioQueue.mutex);
        
        usleep(1000); // 1ms sleep para não sobrecarregar CPU
    }
    
    free(buffer);
    return NULL;
}

int rtmp_stream_init(StreamCallback cb, void* userData) {
    if (processor) return -1;
    
    processor = calloc(1, sizeof(StreamProcessor));
    processor->callback = cb;
    processor->userData = userData;
    
    frame_queue_init(&processor->videoQueue);
    frame_queue_init(&processor->audioQueue);
    
    processor->running = 1;
    pthread_create(&processor->thread, NULL, process_stream, processor);
    
    return 0;
}

void rtmp_stream_destroy(void) {
    if (!processor) return;
    
    processor->running = 0;
    pthread_join(processor->thread, NULL);
    
    frame_queue_destroy(&processor->videoQueue);
    frame_queue_destroy(&processor->audioQueue);
    
    free(processor);
    processor = NULL;
}

int rtmp_stream_push_video(uint8_t* data, size_t length, uint64_t timestamp) {
    if (!processor) return -1;
    
    StreamFrame* frame = malloc(sizeof(StreamFrame));
    frame->data = malloc(length);
    memcpy(frame->data, data, length);
    frame->length = length;
    frame->timestamp = timestamp;
    frame->type = 1;
    
    int dropped = frame_queue_push(&processor->videoQueue, frame);
    if (dropped) {
        processor->videoFrameDropped++;
    }
    
    return dropped;
}

int rtmp_stream_push_audio(uint8_t* data, size_t length, uint64_t timestamp) {
    if (!processor) return -1;
    
    StreamFrame* frame = malloc(sizeof(StreamFrame));
    frame->data = malloc(length);
    memcpy(frame->data, data, length);
    frame->length = length;
    frame->timestamp = timestamp;
    frame->type = 2;
    
    int dropped = frame_queue_push(&processor->audioQueue, frame);
    if (dropped) {
        processor->audioFrameDropped++;
    }
    
    return dropped;
}

void rtmp_stream_get_stats(RTMPStreamStats* stats) {
    if (!processor || !stats) return;
    
    memcpy(stats, &processor->stats, sizeof(RTMPStreamStats));
}