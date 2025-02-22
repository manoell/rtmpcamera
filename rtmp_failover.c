#include "rtmp_failover.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#define MAX_CONNECTION_HISTORY 100
#define HEALTH_CHECK_INTERVAL_MS 1000
#define FAILOVER_THRESHOLD_MS 5000
#define MAX_RETRY_ATTEMPTS 3

typedef struct {
    uint64_t timestamp;
    int type;  // 1 = falha, 2 = recuperação
    char description[256];
} FailureEvent;

typedef struct {
    int running;
    pthread_t monitor_thread;
    pthread_mutex_t mutex;
    
    // Configuração
    FailoverConfig config;
    
    // Estado atual
    int connection_state;
    uint64_t last_health_check;
    int consecutive_failures;
    
    // Histórico de falhas
    FailureEvent history[MAX_CONNECTION_HISTORY];
    int history_count;
    int history_index;
    
    // Callbacks
    FailoverCallback callback;
    void* user_data;
    
    // Métricas
    struct {
        uint32_t total_failures;
        uint32_t successful_recoveries;
        uint32_t failed_recoveries;
        uint64_t total_downtime;
        uint64_t longest_downtime;
        double availability_percentage;
    } metrics;
    
    // Buffer circular para detecção de padrões
    struct {
        uint64_t timestamps[60];  // 1 minuto de histórico
        int values[60];           // valores de saúde
        int index;
        int count;
    } health_buffer;
    
} FailoverController;

static FailoverController* failover = NULL;

static uint64_t get_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void add_failure_event(FailoverController* ctrl, int type, const char* desc) {
    pthread_mutex_lock(&ctrl->mutex);
    
    FailureEvent* event = &ctrl->history[ctrl->history_index];
    event->timestamp = get_current_time_ms();
    event->type = type;
    strncpy(event->description, desc, sizeof(event->description) - 1);
    
    ctrl->history_index = (ctrl->history_index + 1) % MAX_CONNECTION_HISTORY;
    if (ctrl->history_count < MAX_CONNECTION_HISTORY) {
        ctrl->history_count++;
    }
    
    pthread_mutex_unlock(&ctrl->mutex);
}

static void update_health_buffer(FailoverController* ctrl, int health_value) {
    pthread_mutex_lock(&ctrl->mutex);
    
    ctrl->health_buffer.timestamps[ctrl->health_buffer.index] = get_current_time_ms();
    ctrl->health_buffer.values[ctrl->health_buffer.index] = health_value;
    
    ctrl->health_buffer.index = (ctrl->health_buffer.index + 1) % 60;
    if (ctrl->health_buffer.count < 60) {
        ctrl->health_buffer.count++;
    }
    
    pthread_mutex_unlock(&ctrl->mutex);
}

static int detect_failure_pattern(FailoverController* ctrl) {
    if (ctrl->health_buffer.count < 10) return 0;
    
    int poor_health_count = 0;
    int rapid_degradation = 0;
    
    for (int i = 0; i < ctrl->health_buffer.count; i++) {
        if (ctrl->health_buffer.values[i] < 50) {
            poor_health_count++;
        }
        
        if (i > 0) {
            int prev_idx = (i - 1 + 60) % 60;
            if (ctrl->health_buffer.values[i] < ctrl->health_buffer.values[prev_idx] - 20) {
                rapid_degradation++;
            }
        }
    }
    
    return (poor_health_count > ctrl->health_buffer.count / 2) || (rapid_degradation > 2);
}

static void* monitor_connection(void* arg) {
    FailoverController* ctrl = (FailoverController*)arg;
    uint64_t last_check_time = get_current_time_ms();
    
    while (ctrl->running) {
        uint64_t current_time = get_current_time_ms();
        
        if (current_time - last_check_time >= HEALTH_CHECK_INTERVAL_MS) {
            pthread_mutex_lock(&ctrl->mutex);
            
            // Verificar saúde da conexão
            if (ctrl->callback) {
                FailoverStatus status;
                if (ctrl->callback(FAILOVER_CHECK_HEALTH, &status, ctrl->user_data) != 0) {
                    // Problema detectado
                    ctrl->consecutive_failures++;
                    update_health_buffer(ctrl, status.health_score);
                    
                    if (ctrl->consecutive_failures >= ctrl->config.max_failures ||
                        detect_failure_pattern(ctrl)) {
                        
                        // Iniciar failover
                        if (ctrl->connection_state == 1) {
                            ctrl->connection_state = 0;
                            ctrl->metrics.total_failures++;
                            
                            uint64_t downtime_start = get_current_time_ms();
                            add_failure_event(ctrl, 1, status.error_description);
                            
                            // Tentar recuperação
                            int recovered = 0;
                            for (int i = 0; i < MAX_RETRY_ATTEMPTS && !recovered; i++) {
                                FailoverStatus recovery_status;
                                if (ctrl->callback(FAILOVER_ATTEMPT_RECOVERY, 
                                                &recovery_status,
                                                ctrl->user_data) == 0) {
                                    recovered = 1;
                                    ctrl->consecutive_failures = 0;
                                    ctrl->connection_state = 1;
                                    ctrl->metrics.successful_recoveries++;
                                    
                                    uint64_t downtime = get_current_time_ms() - downtime_start;
                                    ctrl->metrics.total_downtime += downtime;
                                    if (downtime > ctrl->metrics.longest_downtime) {
                                        ctrl->metrics.longest_downtime = downtime;
                                    }
                                    
                                    add_failure_event(ctrl, 2, "Connection recovered");
                                }
                            }
                            
                            if (!recovered) {
                                ctrl->metrics.failed_recoveries++;
                                // Notificar falha permanente
								if (ctrl->callback) {
                                    FailoverStatus perm_failure;
                                    ctrl->callback(FAILOVER_PERMANENT_FAILURE, 
                                                &perm_failure,
                                                ctrl->user_data);
                                }
                            }
                        }
                    }
                } else {
                    // Conexão saudável
                    ctrl->consecutive_failures = 0;
                    update_health_buffer(ctrl, status.health_score);
                }
            }
            
            // Atualizar métricas
            uint64_t total_time = current_time - ctrl->config.start_time;
            if (total_time > 0) {
                ctrl->metrics.availability_percentage = 
                    (double)(total_time - ctrl->metrics.total_downtime) / total_time * 100.0;
            }
            
            pthread_mutex_unlock(&ctrl->mutex);
            last_check_time = current_time;
        }
        
        usleep(100 * 1000); // 100ms
    }
    
    return NULL;
}

int rtmp_failover_init(FailoverConfig* config, FailoverCallback cb, void* user_data) {
    if (failover) return -1;
    
    failover = calloc(1, sizeof(FailoverController));
    
    if (config) {
        memcpy(&failover->config, config, sizeof(FailoverConfig));
    } else {
        // Configurações padrão
        failover->config.max_failures = 3;
        failover->config.check_interval = HEALTH_CHECK_INTERVAL_MS;
        failover->config.recovery_timeout = FAILOVER_THRESHOLD_MS;
        failover->config.start_time = get_current_time_ms();
    }
    
    failover->callback = cb;
    failover->user_data = user_data;
    failover->running = 1;
    failover->connection_state = 1;
    
    pthread_mutex_init(&failover->mutex, NULL);
    
    // Iniciar thread de monitoramento
    pthread_create(&failover->monitor_thread, NULL, monitor_connection, failover);
    
    return 0;
}

void rtmp_failover_notify_event(FailoverEventType type, const char* description) {
    if (!failover) return;
    
    pthread_mutex_lock(&failover->mutex);
    
    switch (type) {
        case FAILOVER_EVENT_WARNING:
            // Registrar aviso para análise de padrões
            add_failure_event(failover, 3, description);
            break;
            
        case FAILOVER_EVENT_ERROR:
            // Incrementar contagem de falhas
            failover->consecutive_failures++;
            add_failure_event(failover, 1, description);
            break;
            
        case FAILOVER_EVENT_RECOVERY:
            // Resetar contagem de falhas
            failover->consecutive_failures = 0;
            add_failure_event(failover, 2, description);
            break;
    }
    
    pthread_mutex_unlock(&failover->mutex);
}

int rtmp_failover_get_status(FailoverStatus* status) {
    if (!failover || !status) return -1;
    
    pthread_mutex_lock(&failover->mutex);
    
    // Calcular saúde geral
    int health_score = 100;
    if (failover->consecutive_failures > 0) {
        health_score -= failover->consecutive_failures * 20;
    }
    if (health_score < 0) health_score = 0;
    
    status->connection_state = failover->connection_state;
    status->health_score = health_score;
    status->consecutive_failures = failover->consecutive_failures;
    
    // Copiar métricas
    status->total_failures = failover->metrics.total_failures;
    status->successful_recoveries = failover->metrics.successful_recoveries;
    status->failed_recoveries = failover->metrics.failed_recoveries;
    status->total_downtime = failover->metrics.total_downtime;
    status->longest_downtime = failover->metrics.longest_downtime;
    status->availability_percentage = failover->metrics.availability_percentage;
    
    pthread_mutex_unlock(&failover->mutex);
    
    return 0;
}

int rtmp_failover_get_history(FailureEvent* events, int* count) {
    if (!failover || !events || !count) return -1;
    
    pthread_mutex_lock(&failover->mutex);
    
    int num_events = failover->history_count;
    if (num_events > *count) num_events = *count;
    
    for (int i = 0; i < num_events; i++) {
        int idx = (failover->history_index - 1 - i + MAX_CONNECTION_HISTORY) 
                  % MAX_CONNECTION_HISTORY;
        memcpy(&events[i], &failover->history[idx], sizeof(FailureEvent));
    }
    
    *count = num_events;
    
    pthread_mutex_unlock(&failover->mutex);
    
    return 0;
}

void rtmp_failover_destroy(void) {
    if (!failover) return;
    
    failover->running = 0;
    pthread_join(failover->monitor_thread, NULL);
    pthread_mutex_destroy(&failover->mutex);
    
    free(failover);
    failover = NULL;
}