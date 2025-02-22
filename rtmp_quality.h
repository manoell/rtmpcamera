#ifndef RTMP_QUALITY_H
#define RTMP_QUALITY_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include "rtmp_stream.h"

// Configuração do controlador de qualidade
typedef struct {
    uint32_t max_bitrate;
    uint32_t min_bitrate;
    uint32_t target_latency;
    float quality_priority;  // 0.0 - 1.0, prioridade entre qualidade e latência
} quality_config_t;

// Estatísticas de qualidade
typedef struct {
    uint32_t current_bitrate;
    uint32_t target_bitrate;
    float network_speed;
    float buffer_health;
    float quality_score;
} quality_stats_t;

// Handle opaco para o controlador
typedef struct rtmp_quality_controller rtmp_quality_controller_t;

// Funções principais
rtmp_quality_controller_t *rtmp_quality_controller_create(rtmp_stream_t *stream);
void rtmp_quality_controller_update(rtmp_quality_controller_t *controller);
void rtmp_quality_controller_configure(rtmp_quality_controller_t *controller,
                                     const quality_config_t *config);
quality_stats_t rtmp_quality_controller_get_stats(rtmp_quality_controller_t *controller);
void rtmp_quality_controller_destroy(rtmp_quality_controller_t *controller);

#endif // RTMP_QUALITY_H