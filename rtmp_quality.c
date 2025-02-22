#include "rtmp_quality.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MIN_BITRATE 500000    // 500 Kbps
#define MAX_BITRATE 4000000   // 4 Mbps
#define DEFAULT_BITRATE 2000000 // 2 Mbps
#define BUFFER_WINDOW 3000    // 3 segundos
#define MAX_KEYFRAME_INTERVAL 60 // 2 segundos @ 30fps

typedef struct {
    uint32_t current_bitrate;
    uint32_t target_bitrate;
    uint32_t buffer_health;
    uint64_t last_keyframe;
    double packet_loss_rate;
    double network_latency;
    int resolution_index;
    QualityConfig config;
    QualityCallback callback;
    void* user_data;
    
    // Métricas de qualidade
    struct {
        uint32_t frames_received;
        uint32_t frames_dropped;
        uint32_t bytes_received;
        uint64_t total_latency;
        uint32_t buffer_underruns;
    } metrics;
    
    // Resoluções suportadas
    struct {
        int width;
        int height;
        uint32_t min_bitrate;
    } resolutions[4];
} QualityController;

static QualityController* controller = NULL;

// Inicializa o controlador de qualidade
int rtmp_quality_init(QualityConfig* config, QualityCallback cb, void* user_data) {
    if (controller) return -1;
    
    controller = calloc(1, sizeof(QualityController));
    
    // Configura resoluções suportadas
    controller->resolutions[0] = (struct {int width; int height; uint32_t min_bitrate;}){1920, 1080, 2000000}; // 1080p
    controller->resolutions[1] = (struct {int width; int height; uint32_t min_bitrate;}){1280, 720, 1000000};  // 720p
    controller->resolutions[2] = (struct {int width; int height; uint32_t min_bitrate;}){854, 480, 500000};    // 480p
    controller->resolutions[3] = (struct {int width; int height; uint32_t min_bitrate;}){640, 360, 250000};    // 360p

    if (config) {
        memcpy(&controller->config, config, sizeof(QualityConfig));
    } else {
        // Configurações padrão
        controller->config.target_latency = 500;     // 500ms
        controller->config.max_latency = 2000;       // 2s
        controller->config.buffer_size = BUFFER_WINDOW;
        controller->config.adaptive_bitrate = 1;
        controller->config.initial_bitrate = DEFAULT_BITRATE;
    }
    
    controller->callback = cb;
    controller->user_data = user_data;
    controller->current_bitrate = controller->config.initial_bitrate;
    controller->target_bitrate = controller->config.initial_bitrate;
    controller->resolution_index = 0;
    
    return 0;
}

// Atualiza métricas e ajusta qualidade
void rtmp_quality_update(QualityMetrics* metrics) {
    if (!controller) return;
    
    // Atualiza métricas
    controller->packet_loss_rate = metrics->packet_loss;
    controller->network_latency = metrics->network_latency;
    controller->buffer_health = metrics->buffer_health;
    
    // Verifica saúde do buffer
    if (controller->buffer_health < controller->config.buffer_size / 2) {
        controller->metrics.buffer_underruns++;
    }
    
    // Calcula ajustes necessários
    int quality_change = 0;
    uint32_t new_bitrate = controller->current_bitrate;
    int new_resolution = controller->resolution_index;
    
    // Ajuste baseado em perda de pacotes
    if (controller->packet_loss_rate > 0.05) { // >5% perda
        new_bitrate = (uint32_t)(new_bitrate * 0.8); // Reduz 20%
        quality_change = 1;
    } else if (controller->packet_loss_rate < 0.01) { // <1% perda
        new_bitrate = (uint32_t)(new_bitrate * 1.1); // Aumenta 10%
        quality_change = 1;
    }
    
    // Ajuste baseado em latência
    if (controller->network_latency > controller->config.max_latency) {
        new_bitrate = (uint32_t)(new_bitrate * 0.7); // Reduz 30%
        new_resolution++; // Reduz resolução
        quality_change = 1;
    } else if (controller->network_latency < controller->config.target_latency) {
        new_bitrate = (uint32_t)(new_bitrate * 1.2); // Aumenta 20%
        if (new_resolution > 0) new_resolution--;
        quality_change = 1;
    }
    
    // Limites de bitrate
    if (new_bitrate > MAX_BITRATE) new_bitrate = MAX_BITRATE;
    if (new_bitrate < MIN_BITRATE) new_bitrate = MIN_BITRATE;
    
    // Limites de resolução
    if (new_resolution > 3) new_resolution = 3;
    if (new_resolution < 0) new_resolution = 0;
    
    // Verifica se bitrate está adequado para resolução
    if (new_bitrate < controller->resolutions[new_resolution].min_bitrate) {
        new_resolution++;
    }
    
    // Aplica mudanças se necessário
    if (quality_change && controller->config.adaptive_bitrate) {
        QualityUpdate update = {
            .new_bitrate = new_bitrate,
            .width = controller->resolutions[new_resolution].width,
            .height = controller->resolutions[new_resolution].height,
            .force_keyframe = (new_resolution != controller->resolution_index)
        };
        
        controller->current_bitrate = new_bitrate;
        controller->resolution_index = new_resolution;
        
        if (controller->callback) {
            controller->callback(&update, controller->user_data);
        }
    }
}

// Força um keyframe baseado em intervalo
void rtmp_quality_check_keyframe(uint64_t timestamp) {
    if (!controller) return;
    
    if (timestamp - controller->last_keyframe > MAX_KEYFRAME_INTERVAL) {
        QualityUpdate update = {
            .new_bitrate = controller->current_bitrate,
            .width = controller->resolutions[controller->resolution_index].width,
            .height = controller->resolutions[controller->resolution_index].height,
            .force_keyframe = 1
        };
        
        controller->last_keyframe = timestamp;
        
        if (controller->callback) {
            controller->callback(&update, controller->user_data);
        }
    }
}

// Obtém estatísticas atuais
void rtmp_quality_get_stats(QualityStats* stats) {
    if (!controller || !stats) return;
    
    stats->current_bitrate = controller->current_bitrate;
    stats->packet_loss = controller->packet_loss_rate;
    stats->network_latency = controller->network_latency;
    stats->buffer_health = controller->buffer_health;
    stats->resolution_width = controller->resolutions[controller->resolution_index].width;
    stats->resolution_height = controller->resolutions[controller->resolution_index].height;
    stats->buffer_underruns = controller->metrics.buffer_underruns;
}

void rtmp_quality_destroy(void) {
    if (controller) {
        free(controller);
        controller = NULL;
    }
}