#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "rtmp_server_integration.h"
#include "rtmp_core.h"
#include "rtmp_diagnostics.h"

#define DEFAULT_PORT 1935
#define DEFAULT_CHUNK_SIZE 4096
#define RECONNECT_TIMEOUT 1000    // 1 segundo
#define CONNECTION_TIMEOUT 5000   // 5 segundos
#define HEALTH_CHECK_INTERVAL 100 // 100ms

// Estrutura de contexto do servidor
struct rtmp_server_context {
    char *server_url;
    uint16_t port;
    bool is_connected;
    bool is_local_network;
    rtmp_stream_t *stream;
    rtmp_failover_t *failover;
    rtmp_quality_controller_t *quality;
    
    // Estatísticas
    uint32_t connect_attempts;
    uint32_t last_health_check;
    uint32_t last_connect_time;
    server_stats_t stats;
    
    // Callbacks
    void (*callback)(server_event_t event, void *data, void *ctx);
    void *callback_ctx;
    
    // Thread de monitoramento
    pthread_t monitor_thread;
    pthread_mutex_t lock;
    bool monitor_running;
};

// Detecta se é rede local
static bool is_local_network(const char *url) {
    return strstr(url, "localhost") || 
           strstr(url, "127.0.0.1") || 
           strstr(url, "192.168.") ||
           strstr(url, "10.") ||
           strstr(url, "172.16.");
}

// Thread de monitoramento
static void *monitor_thread(void *arg) {
    rtmp_server_context_t *context = (rtmp_server_context_t *)arg;
    uint32_t current_time;
    
    while (context->monitor_running) {
        current_time = get_timestamp();
        
        pthread_mutex_lock(&context->lock);
        
        // Verifica saúde da conexão
        if (current_time - context->last_health_check >= HEALTH_CHECK_INTERVAL) {
            if (context->is_connected) {
                stream_stats_t stream_stats = rtmp_stream_get_stats(context->stream);
                quality_stats_t quality_stats = rtmp_quality_controller_get_stats(context->quality);
                
                // Atualiza estatísticas
                context->stats.current_bitrate = quality_stats.current_bitrate;
                context->stats.buffer_health = stream_stats.buffer_health;
                context->stats.current_fps = stream_stats.current_fps;
                
                // Verifica problemas
                if (stream_stats.buffer_health < 0.5 || stream_stats.current_fps < 20.0) {
                    rtmp_diagnostics_log(LOG_WARN, "Problemas de performance detectados - Buffer: %.2f, FPS: %.2f",
                                       stream_stats.buffer_health, stream_stats.current_fps);
                    
                    if (context->callback) {
                        context->callback(SERVER_WARNING, "Performance issues detected", context->callback_ctx);
                    }
                }
            }
            context->last_health_check = current_time;
        }
        
        // Verifica timeout de conexão
        if (!context->is_connected && 
            (current_time - context->last_connect_time >= CONNECTION_TIMEOUT)) {
            rtmp_diagnostics_log(LOG_ERROR, "Timeout de conexão - Tentando reconectar");
            rtmp_server_reconnect(context);
        }
        
        pthread_mutex_unlock(&context->lock);
        usleep(10000); // 10ms para não sobrecarregar a CPU
    }
    
    return NULL;
}

rtmp_server_context_t *rtmp_server_create(const char *url, uint16_t port) {
    rtmp_server_context_t *context = calloc(1, sizeof(rtmp_server_context_t));
    if (!context) return NULL;
    
    context->server_url = strdup(url);
    context->port = port ? port : DEFAULT_PORT;
    context->is_local_network = is_local_network(url);
    
    // Inicializa componentes
    context->stream = rtmp_stream_create(NULL);
    context->failover = rtmp_failover_create(url, NULL);
    context->quality = rtmp_quality_controller_create(context->stream);
    
    // Configuração inicial otimizada para rede local
    quality_config_t qconfig = {
        .max_bitrate = context->is_local_network ? 12000000 : 4000000,
        .min_bitrate = context->is_local_network ? 1000000 : 500000,
        .target_latency = context->is_local_network ? 50 : 100,
        .quality_priority = 0.7f
    };
    rtmp_quality_controller_configure(context->quality, &qconfig);
    
    pthread_mutex_init(&context->lock, NULL);
    
    return context;
}

int rtmp_server_connect(rtmp_server_context_t *context) {
    if (!context) return -1;
    
    pthread_mutex_lock(&context->lock);
    
    // Formata URL completa
    char url[256];
    snprintf(url, sizeof(url), "rtmp://%s:%d/live", context->server_url, context->port);
    
    // Configura parâmetros otimizados
    server_config_t config = {
        .chunk_size = DEFAULT_CHUNK_SIZE,
        .window_ack_size = context->is_local_network ? 2500000 : 1000000,
        .peer_bandwidth = context->is_local_network ? 2500000 : 1000000,
        .callback = context->callback,
        .callback_ctx = context->callback_ctx
    };
    
    // Inicia conexão
    if (rtmp_stream_connect(context->stream, url) != 0) {
        rtmp_diagnostics_log(LOG_ERROR, "Falha ao conectar ao servidor RTMP");
        pthread_mutex_unlock(&context->lock);
        return -1;
    }
    
    // Inicia failover e controle de qualidade
    rtmp_failover_start(context->failover);
    
    // Inicia thread de monitoramento
    context->monitor_running = true;
    if (pthread_create(&context->monitor_thread, NULL, monitor_thread, context) != 0) {
        rtmp_diagnostics_log(LOG_ERROR, "Falha ao iniciar thread de monitoramento");
        rtmp_stream_disconnect(context->stream);
        pthread_mutex_unlock(&context->lock);
        return -1;
    }
    
    context->is_connected = true;
    context->last_connect_time = get_timestamp();
    context->connect_attempts++;
    
    rtmp_diagnostics_log(LOG_INFO, "Conectado com sucesso ao servidor RTMP: %s", url);
    
    if (context->callback) {
        context->callback(SERVER_CONNECTED, NULL, context->callback_ctx);
    }
    
    pthread_mutex_unlock(&context->lock);
    return 0;
}

int rtmp_server_reconnect(rtmp_server_context_t *context) {
    if (!context) return -1;
    
    pthread_mutex_lock(&context->lock);
    
    rtmp_diagnostics_log(LOG_INFO, "Tentando reconexão ao servidor RTMP...");
    
    // Limpa estado atual
    rtmp_stream_disconnect(context->stream);
    context->is_connected = false;
    
    // Espera timeout
    usleep(RECONNECT_TIMEOUT * 1000);
    
    // Tenta reconectar
    int result = rtmp_server_connect(context);
    
    pthread_mutex_unlock(&context->lock);
    return result;
}

void rtmp_server_configure(rtmp_server_context_t *context, const server_config_t *config) {
    if (!context || !config) return;
    
    pthread_mutex_lock(&context->lock);
    
    // Aplica configurações considerando rede local
    quality_config_t qconfig = {
        .max_bitrate = context->is_local_network ? 
            MAX(config->max_bitrate, 12000000) : config->max_bitrate,
        .min_bitrate = context->is_local_network ? 
            MAX(config->min_bitrate, 1000000) : config->min_bitrate,
        .target_latency = context->is_local_network ? 
            MIN(config->target_latency, 50) : config->target_latency,
        .quality_priority = config->quality_priority
    };
    
    rtmp_quality_controller_configure(context->quality, &qconfig);
    
    // Configura failover se necessário
    if (config->backup_url) {
        rtmp_failover_destroy(context->failover);
        context->failover = rtmp_failover_create(context->server_url, config->backup_url);
        rtmp_failover_start(context->failover);
    }
    
    context->callback = config->callback;
    context->callback_ctx = config->callback_ctx;
    
    rtmp_diagnostics_log(LOG_INFO, "Configuração do servidor atualizada");
    
    pthread_mutex_unlock(&context->lock);
}

server_stats_t rtmp_server_get_stats(rtmp_server_context_t *context) {
    server_stats_t stats = {0};
    if (!context) return stats;
    
    pthread_mutex_lock(&context->lock);
    stats = context->stats;
    pthread_mutex_unlock(&context->lock);
    
    return stats;
}

void rtmp_server_destroy(rtmp_server_context_t *context) {
    if (!context) return;
    
    // Para thread de monitoramento
    context->monitor_running = false;
    pthread_join(context->monitor_thread, NULL);
    
    // Desconecta e limpa recursos
    rtmp_stream_disconnect(context->stream);
    rtmp_failover_destroy(context->failover);
    rtmp_quality_controller_destroy(context->quality);
    rtmp_stream_destroy(context->stream);
    
    pthread_mutex_destroy(&context->lock);
    free(context->server_url);
    free(context);
    
    rtmp_diagnostics_log(LOG_INFO, "Servidor RTMP finalizado");
}