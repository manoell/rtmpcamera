#ifndef RTMP_STREAM_H
#define RTMP_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include "rtmp_core.h"

// Configurações otimizadas para rede local
#define STREAM_BUFFER_SIZE (1024 * 1024)  // 1MB buffer
#define MAX_FRAME_SIZE (1920 * 1080 * 4)  // 1080p max
#define MAX_QUEUE_SIZE 60                  // 2 segundos em 30fps
#define MIN_CACHE_TIME 50                  // 50ms
#define MAX_CACHE_TIME 500                 // 500ms

// Tipo de codec
typedef enum {
    VIDEO_CODEC_UNKNOWN = 0,
    VIDEO_CODEC_H264,
    VIDEO_CODEC_H265
} video_codec_t;

// Estrutura de frame
typedef struct {
    uint8_t *data;
    size_t length;
    uint32_t timestamp;
    bool is_keyframe;
    bool is_sequence_header;
    video_codec_t codec;
} video_frame_t;

// Estatísticas do stream
typedef struct {
    video_codec_t video_codec;
    float current_fps;
    float target_fps;
    float buffer_usage;
    float buffer_health;
    uint32_t cache_time;
    uint32_t dropped_frames;
    uint32_t total_frames;
    uint32_t current_bitrate;
    uint32_t target_bitrate;
    float quality_score;
} stream_stats_t;

// Configuração do stream
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t target_fps;
    uint32_t initial_bitrate;
    uint32_t buffer_size;
    bool hardware_acceleration;
    bool is_local_network;
} stream_config_t;

// Handle opaco para o stream
typedef struct rtmp_stream rtmp_stream_t;

// Callbacks
typedef void (*frame_callback_t)(video_frame_t *frame, void *ctx);
typedef void (*status_callback_t)(stream_stats_t stats, void *ctx);

// Funções principais
rtmp_stream_t *rtmp_stream_create(const stream_config_t *config);
int rtmp_stream_connect(rtmp_stream_t *stream, const char *url);
void rtmp_stream_disconnect(rtmp_stream_t *stream);
void rtmp_stream_destroy(rtmp_stream_t *stream);

// Controle de frames
video_frame_t *rtmp_stream_get_next_frame(rtmp_stream_t *stream);
void rtmp_stream_release_frame(video_frame_t *frame);
int rtmp_stream_queue_frame(rtmp_stream_t *stream, const uint8_t *data, size_t length, uint32_t timestamp);

// Controle de qualidade
void rtmp_stream_set_bitrate(rtmp_stream_t *stream, uint32_t bitrate);
void rtmp_stream_set_fps(rtmp_stream_t *stream, uint32_t fps);
void rtmp_stream_set_buffer_size(rtmp_stream_t *stream, uint32_t size);

// Monitoramento
stream_stats_t rtmp_stream_get_stats(rtmp_stream_t *stream);
void rtmp_stream_set_callbacks(rtmp_stream_t *stream, frame_callback_t frame_cb, status_callback_t status_cb, void *ctx);

// Utilidades
const char *rtmp_stream_get_codec_name(video_codec_t codec);
bool rtmp_stream_is_connected(rtmp_stream_t *stream);
float rtmp_stream_get_latency(rtmp_stream_t *stream);

#endif // RTMP_STREAM_H