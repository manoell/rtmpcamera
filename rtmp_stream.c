#include "rtmp_stream.h"
#include "rtmp_core.h"
#include "rtmp_quality.h"
#include "rtmp_chunk.h"
#include "rtmp_diagnostics.h"
#include <string.h>
#include <pthread.h>
#include <sys/time.h>

#define STREAM_BUFFER_SIZE (1024 * 1024)  // 1MB buffer
#define MAX_QUEUE_SIZE 60  // Maximum frames to queue
#define MIN_BUFFER_TIME 100  // Minimum buffer in milliseconds
#define MAX_BUFFER_TIME 2000 // Maximum buffer in milliseconds

typedef struct {
    uint8_t *data;
    size_t size;
    int64_t timestamp;
    bool is_keyframe;
} StreamFrame;

typedef struct {
    StreamFrame *frames;
    int head;
    int tail;
    int count;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} FrameQueue;

struct RTMPStream {
    // Connection properties
    char url[256];
    bool connected;
    bool running;
    
    // Video properties
    uint32_t video_width;
    uint32_t video_height;
    uint32_t video_fps;
    uint32_t video_bitrate;
    uint32_t video_quality;
    
    // Buffer management
    uint8_t *stream_buffer;
    size_t buffer_size;
    size_t buffer_position;
    
    // Frame queue
    FrameQueue frame_queue;
    
    // Threading
    pthread_t stream_thread;
    pthread_mutex_t stream_mutex;
    
    // Statistics
    struct {
        uint32_t current_fps;
        uint32_t current_bitrate;
        uint32_t buffer_ms;
        uint32_t quality_percent;
        uint32_t dropped_frames;
        struct timeval last_stats_update;
    } stats;
    
    // Callbacks
    rtmp_stream_callback_t event_callback;
    void *callback_context;
};

static void init_frame_queue(FrameQueue *queue) {
    queue->frames = (StreamFrame*)calloc(MAX_QUEUE_SIZE, sizeof(StreamFrame));
    queue->head = queue->tail = queue->count = 0;
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond, NULL);
}

static void destroy_frame_queue(FrameQueue *queue) {
    pthread_mutex_lock(&queue->mutex);
    for (int i = 0; i < MAX_QUEUE_SIZE; i++) {
        free(queue->frames[i].data);
    }
    free(queue->frames);
    pthread_mutex_unlock(&queue->mutex);
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond);
}

static bool enqueue_frame(FrameQueue *queue, const uint8_t *data, size_t size, int64_t timestamp, bool is_keyframe) {
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->count >= MAX_QUEUE_SIZE) {
        pthread_mutex_unlock(&queue->mutex);
        return false;
    }
    
    StreamFrame *frame = &queue->frames[queue->tail];
    frame->data = (uint8_t*)malloc(size);
    memcpy(frame->data, data, size);
    frame->size = size;
    frame->timestamp = timestamp;
    frame->is_keyframe = is_keyframe;
    
    queue->tail = (queue->tail + 1) % MAX_QUEUE_SIZE;
    queue->count++;
    
    pthread_cond_signal(&queue->cond);
    pthread_mutex_unlock(&queue->mutex);
    return true;
}

static bool dequeue_frame(FrameQueue *queue, StreamFrame *frame) {
    pthread_mutex_lock(&queue->mutex);
    
    while (queue->count == 0) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
    }
    
    StreamFrame *src_frame = &queue->frames[queue->head];
    frame->data = src_frame->data;
    frame->size = src_frame->size;
    frame->timestamp = src_frame->timestamp;
    frame->is_keyframe = src_frame->is_keyframe;
    
    src_frame->data = NULL;  // Ownership transferred
    
    queue->head = (queue->head + 1) % MAX_QUEUE_SIZE;
    queue->count--;
    
    pthread_mutex_unlock(&queue->mutex);
    return true;
}

static void *stream_thread_func(void *arg) {
    RTMPStream *stream = (RTMPStream*)arg;
    StreamFrame frame;
    
    while (stream->running) {
        if (dequeue_frame(&stream->frame_queue, &frame)) {
            pthread_mutex_lock(&stream->stream_mutex);
            
            // Process frame
            if (stream->buffer_position + frame.size <= STREAM_BUFFER_SIZE) {
                memcpy(stream->stream_buffer + stream->buffer_position, frame.data, frame.size);
                stream->buffer_position += frame.size;
                
                // Update statistics
                struct timeval now;
                gettimeofday(&now, NULL);
                int64_t time_diff = (now.tv_sec - stream->stats.last_stats_update.tv_sec) * 1000 +
                                  (now.tv_usec - stream->stats.last_stats_update.tv_usec) / 1000;
                
                if (time_diff >= 1000) {  // Update stats every second
                    stream->stats.current_bitrate = (uint32_t)((stream->buffer_position * 8 * 1000) / time_diff);
                    stream->stats.buffer_ms = (uint32_t)((stream->buffer_position * 1000) / stream->video_bitrate);
                    stream->stats.last_stats_update = now;
                    stream->buffer_position = 0;
                }
            }
            
            // Notify event
            if (stream->event_callback) {
                RTMPStreamEvent event = {
                    .type = RTMP_EVENT_FRAME_PROCESSED,
                    .timestamp = frame.timestamp,
                    .data = frame.data,
                    .data_size = frame.size
                };
                stream->event_callback(&event, stream->callback_context);
            }
            
            pthread_mutex_unlock(&stream->stream_mutex);
            free(frame.data);
        }
    }
    
    return NULL;
}

RTMPStream* rtmp_stream_create(const char *url) {
    RTMPStream *stream = (RTMPStream*)calloc(1, sizeof(RTMPStream));
    if (!stream) return NULL;
    
    strncpy(stream->url, url, sizeof(stream->url) - 1);
    stream->stream_buffer = (uint8_t*)malloc(STREAM_BUFFER_SIZE);
    
    init_frame_queue(&stream->frame_queue);
    pthread_mutex_init(&stream->stream_mutex, NULL);
    
    gettimeofday(&stream->stats.last_stats_update, NULL);
    
    return stream;
}

void rtmp_stream_destroy(RTMPStream *stream) {
    if (!stream) return;
    
    stream->running = false;
    pthread_join(stream->stream_thread, NULL);
    
    destroy_frame_queue(&stream->frame_queue);
    pthread_mutex_destroy(&stream->stream_mutex);
    
    free(stream->stream_buffer);
    free(stream);
}

bool rtmp_stream_start(RTMPStream *stream) {
    if (!stream || stream->running) return false;
    
    stream->running = true;
    int result = pthread_create(&stream->stream_thread, NULL, stream_thread_func, stream);
    
    if (result != 0) {
        stream->running = false;
        return false;
    }
    
    return true;
}

void rtmp_stream_stop(RTMPStream *stream) {
    if (!stream || !stream->running) return;
    stream->running = false;
}

bool rtmp_stream_push_frame(RTMPStream *stream, const uint8_t *data, size_t size, int64_t timestamp, bool is_keyframe) {
    if (!stream || !data || size == 0) return false;
    
    return enqueue_frame(&stream->frame_queue, data, size, timestamp, is_keyframe);
}

void rtmp_stream_set_callback(RTMPStream *stream, rtmp_stream_callback_t callback, void *context) {
    if (!stream) return;
    
    pthread_mutex_lock(&stream->stream_mutex);
    stream->event_callback = callback;
    stream->callback_context = context;
    pthread_mutex_unlock(&stream->stream_mutex);
}

void rtmp_stream_get_stats(RTMPStream *stream, RTMPStreamStats *stats) {
    if (!stream || !stats) return;
    
    pthread_mutex_lock(&stream->stream_mutex);
    memcpy(stats, &stream->stats, sizeof(RTMPStreamStats));
    pthread_mutex_unlock(&stream->stream_mutex);
}

bool rtmp_stream_set_video_params(RTMPStream *stream, uint32_t width, uint32_t height, uint32_t fps, uint32_t bitrate) {
    if (!stream) return false;
    
    pthread_mutex_lock(&stream->stream_mutex);
    stream->video_width = width;
    stream->video_height = height;
    stream->video_fps = fps;
    stream->video_bitrate = bitrate;
    pthread_mutex_unlock(&stream->stream_mutex);
    
    return true;
}

bool rtmp_stream_is_connected(RTMPStream *stream) {
    return stream ? stream->connected : false;
}

void rtmp_stream_set_connected(RTMPStream *stream, bool connected) {
    if (!stream) return;
    stream->connected = connected;
}