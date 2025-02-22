#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include "rtmp_quality.h"
#include "rtmp_core.h"
#include "rtmp_diagnostics.h"

// Configurações otimizadas para rede local
#define LOCAL_MAX_BITRATE 12000000     // 12 Mbps
#define LOCAL_MIN_BITRATE 1000000      // 1 Mbps
#define LOCAL_TARGET_LATENCY 50        // 50ms
#define QUALITY_CHECK_INTERVAL 100     // 100ms
#define NETWORK_CHECK_INTERVAL 50      // 50ms
#define BUFFER_THRESHOLD_HIGH 0.9
#define BUFFER_THRESHOLD_LOW 0.3
#define FPS_TARGET 30.0
#define FPS_MIN_ACCEPTABLE 25.0

struct rtmp_quality_controller {
    rtmp_stream_t *stream;
    quality_config_t config;
    quality_stats_t stats;
    
    uint32_t last_quality_check;
    uint32_t last_network_check;
    uint32_t current_bitrate;
    uint32_t target_bitrate;
    
    float network_speed;
    float buffer_health;
    float average_fps;
    uint32_t frame_count;
    uint32_t drop_count;
    
    bool is_local_network;
    bool is_adjusting;
    
    pthread_mutex_t lock;
};

static uint32_t get_timestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

rtmp_quality_controller_t *rtmp_quality_controller_create(rtmp_stream_t *stream) {
    rtmp_quality_controller_t *controller = calloc(1, sizeof(rtmp_quality_controller_t));
    if (!controller) return NULL;
    
    controller->stream = stream;
    controller->current_bitrate = LOCAL_MAX_BITRATE;
    controller->target_bitrate = LOCAL_MAX_BITRATE;
    controller->is_local_network = true;
    
    // Configuração inicial otimizada
    controller->config.max_bitrate = LOCAL_MAX_BITRATE;
    controller->config.min_bitrate = LOCAL_MIN_BITRATE;
    controller->config.target_latency = LOCAL_TARGET_LATENCY;
    controller->config.quality_priority = 0.7f;
    
    pthread_mutex_init(&controller->lock, NULL);
    
    rtmp_diagnostics_log(LOG_INFO, "Controlador de qualidade inicializado - Modo: Local");
    
    return controller;
}

static void adjust_for_network_conditions(rtmp_quality_controller_t *controller) {
    float network_stability = controller->network_speed / (float)controller->config.max_bitrate;
    float buffer_stability = controller->buffer_health;
    
    // Calcula FPS médio
    float current_fps = (float)controller->frame_count / 
                       ((float)(get_timestamp() - controller->last_quality_check) / 1000.0f);
    controller->average_fps = (controller->average_fps * 0.7f) + (current_fps * 0.3f);
    
    // Análise de condições
    bool network_good = network_stability > 0.9f;
    bool buffer_good = buffer_stability > 0.7f;
    bool fps_good = controller->average_fps > FPS_MIN_ACCEPTABLE;
    
    uint32_t ideal_bitrate = controller->current_bitrate;
    
    if (network_good && buffer_good && fps_good) {
        // Condições ótimas - pode aumentar qualidade
        if (controller->current_bitrate < controller->config.max_bitrate) {
            ideal_bitrate = MIN(controller->current_bitrate * 1.2, 
                              controller->config.max_bitrate);
        }
    } else if (!network_good || !buffer_good || !fps_good) {
        // Problemas detectados - reduz qualidade
        ideal_bitrate = MAX(controller->current_bitrate * 0.8,
                          controller->config.min_bitrate);
        
        rtmp_diagnostics_log(LOG_WARN, 
            "Ajustando qualidade - Network: %.2f, Buffer: %.2f, FPS: %.2f",
            network_stability, buffer_stability, controller->average_fps);
    }
    
    // Ajusta gradualmente
    if (ideal_bitrate != controller->target_bitrate) {
        controller->target_bitrate = ideal_bitrate;
        controller->is_adjusting = true;
        
        rtmp_diagnostics_log(LOG_INFO, 
            "Novo target bitrate: %d kbps", controller->target_bitrate / 1000);
    }
}

void rtmp_quality_controller_update(rtmp_quality_controller_t *controller) {
    if (!controller) return;
    
    pthread_mutex_lock(&controller->lock);
    
    uint32_t current_time = get_timestamp();
    
    // Verifica rede
    if (current_time - controller->last_network_check >= NETWORK_CHECK_INTERVAL) {
        controller->network_speed = rtmp_network_get_speed(controller->stream);
        controller->buffer_health = rtmp_buffer_get_health(controller->stream);
        controller->last_network_check = current_time;
    }
    
    // Ajusta qualidade
    if (current_time - controller->last_quality_check >= QUALITY_CHECK_INTERVAL) {
        adjust_for_network_conditions(controller);
        
        // Atualiza estatísticas
        controller->stats.current_bitrate = controller->current_bitrate;
        controller->stats.target_bitrate = controller->target_bitrate;
        controller->stats.network_speed = controller->network_speed;
        controller->stats.buffer_health = controller->buffer_health;
        controller->stats.quality_score = (controller->network_speed / controller->config.max_bitrate) * 
                                        controller->buffer_health;
        
        // Reseta contadores
        controller->frame_count = 0;
        controller->drop_count = 0;
        controller->last_quality_check = current_time;
    }
    
    // Aplica ajustes gradualmente
    if (controller->is_adjusting) {
        float adjustment_factor = 1.0f;
        
        if (controller->current_bitrate < controller->target_bitrate) {
            controller->current_bitrate = MIN(
                controller->current_bitrate + (uint32_t)(controller->config.max_bitrate * 0.1f * adjustment_factor),
                controller->target_bitrate
            );
        } else if (controller->current_bitrate > controller->target_bitrate) {
            controller->current_bitrate = MAX(
                controller->current_bitrate - (uint32_t)(controller->config.max_bitrate * 0.1f * adjustment_factor),
                controller->target_bitrate
            );
        }
        
        if (controller->current_bitrate == controller->target_bitrate) {
            controller->is_adjusting = false;
        }
        
        // Aplica novo bitrate
        rtmp_stream_set_bitrate(controller->stream, controller->current_bitrate);
    }
    
    pthread_mutex_unlock(&controller->lock);
}

void rtmp_quality_controller_configure(rtmp_quality_controller_t *controller,
                                     const quality_config_t *config) {
    if (!controller || !config) return;
    
    pthread_mutex_lock(&controller->lock);
    
    controller->config = *config;
    
    // Ajusta limites baseado no tipo de rede
    if (controller->is_local_network) {
        controller->config.max_bitrate = MIN(config->max_bitrate, LOCAL_MAX_BITRATE);
        controller->config.min_bitrate = MAX(config->min_bitrate, LOCAL_MIN_BITRATE);
        controller->config.target_latency = MIN(config->target_latency, LOCAL_TARGET_LATENCY);
    } else {
        controller->config.max_bitrate = MIN(config->max_bitrate, LOCAL_MAX_BITRATE/2);
        controller->config.min_bitrate = MAX(config->min_bitrate, LOCAL_MIN_BITRATE*2);
    }
    
    // Recalcula target baseado na nova configuração
    controller->target_bitrate = MIN(controller->current_bitrate, 
                                   controller->config.max_bitrate);
    
    rtmp_diagnostics_log(LOG_INFO, 
        "Configuração atualizada - Max: %d kbps, Min: %d kbps, Latency: %d ms",
        controller->config.max_bitrate/1000,
        controller->config.min_bitrate/1000,
        controller->config.target_latency);
    
    pthread_mutex_unlock(&controller->lock);
}

quality_stats_t rtmp_quality_controller_get_stats(rtmp_quality_controller_t *controller) {
    quality_stats_t stats = {0};
    if (!controller) return stats;
    
    pthread_mutex_lock(&controller->lock);
    stats = controller->stats;
    pthread_mutex_unlock(&controller->lock);
    
    return stats;
}

void rtmp_quality_controller_destroy(rtmp_quality_controller_t *controller) {
    if (!controller) return;
    
    pthread_mutex_destroy(&controller->lock);
    free(controller);
    
    rtmp_diagnostics_log(LOG_INFO, "Controlador de qualidade finalizado");
}