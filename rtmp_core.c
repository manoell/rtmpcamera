#include "rtmp_core.h"
#include "rtmp_utils.h"
#include "rtmp_diagnostics.h"
#include "rtmp_handshake.h"
#include "rtmp_protocol.h"
#include "rtmp_stream.h"
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Configuration constants
#define RTMP_DEFAULT_PORT 1935
#define RTMP_MAX_CONNECTIONS 100
#define RTMP_BUFFER_SIZE 4096
#define RTMP_HEARTBEAT_INTERVAL 30
#define RTMP_RECOVERY_ATTEMPTS 3

// Server context
typedef struct {
    int active_connections;
    pthread_mutex_t conn_mutex;
    pthread_mutex_t stats_mutex;
    rtmp_connection_t *connections[RTMP_MAX_CONNECTIONS];
    int server_socket;
    int running;
    rtmp_config_t config;
    rtmp_stats_t stats;
    int recovery_mode;
} rtmp_server_context_t;

static rtmp_server_context_t server_ctx;

// Forward declarations
static void* rtmp_core_accept_loop(void *arg);
static void* rtmp_core_monitor_loop(void *arg);
static int rtmp_core_recover_connection(rtmp_connection_t *conn);

// Initialize the RTMP core
int rtmp_core_init(void) {
    memset(&server_ctx, 0, sizeof(server_ctx));
    
    // Initialize mutexes
    if (pthread_mutex_init(&server_ctx.conn_mutex, NULL) != 0) {
        rtmp_log_error("Failed to initialize connection mutex");
        return RTMP_ERROR_INIT_FAILED;
    }
    
    if (pthread_mutex_init(&server_ctx.stats_mutex, NULL) != 0) {
        pthread_mutex_destroy(&server_ctx.conn_mutex);
        rtmp_log_error("Failed to initialize stats mutex");
        return RTMP_ERROR_INIT_FAILED;
    }
    
    // Set default configuration
    server_ctx.config.port = RTMP_DEFAULT_PORT;
    server_ctx.config.max_connections = RTMP_MAX_CONNECTIONS;
    server_ctx.config.buffer_size = RTMP_BUFFER_SIZE;
    server_ctx.config.enable_recovery = 1;
    
    // Initialize statistics
    server_ctx.stats.start_time = time(NULL);
    server_ctx.stats.total_connections = 0;
    server_ctx.stats.active_streams = 0;
    server_ctx.stats.bytes_received = 0;
    server_ctx.stats.bytes_sent = 0;
    server_ctx.stats.dropped_frames = 0;
    
    rtmp_log_info("RTMP Core initialized successfully");
    return RTMP_SUCCESS;
}

// Start the RTMP server
int rtmp_core_start(void) {
    if (server_ctx.running) {
        rtmp_log_warning("Server already running");
        return RTMP_ERROR_ALREADY_RUNNING;
    }
    
    // Create server socket
    struct sockaddr_in server_addr;
    server_ctx.server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_ctx.server_socket < 0) {
        rtmp_log_error("Failed to create server socket");
        return RTMP_ERROR_SOCKET_CREATE;
    }
    
    // Configure socket
    int opt = 1;
    setsockopt(server_ctx.server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server_ctx.config.port);
    
    if (bind(server_ctx.server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(server_ctx.server_socket);
        rtmp_log_error("Failed to bind server socket");
        return RTMP_ERROR_SOCKET_BIND;
    }
    
    // Listen for connections
    if (listen(server_ctx.server_socket, RTMP_MAX_CONNECTIONS) < 0) {
        close(server_ctx.server_socket);
        rtmp_log_error("Failed to listen on server socket");
        return RTMP_ERROR_SOCKET_LISTEN;
    }
    
    server_ctx.running = 1;
    
    // Start accept thread
    pthread_t accept_thread;
    if (pthread_create(&accept_thread, NULL, rtmp_core_accept_loop, NULL) != 0) {
        server_ctx.running = 0;
        close(server_ctx.server_socket);
        rtmp_log_error("Failed to create accept thread");
        return RTMP_ERROR_THREAD_CREATE;
    }
    
    // Start monitor thread
    pthread_t monitor_thread;
    if (pthread_create(&monitor_thread, NULL, rtmp_core_monitor_loop, NULL) != 0) {
        server_ctx.running = 0;
        close(server_ctx.server_socket);
        rtmp_log_error("Failed to create monitor thread");
        return RTMP_ERROR_THREAD_CREATE;
    }
    
    rtmp_log_info("RTMP Server started on port %d", server_ctx.config.port);
    return RTMP_SUCCESS;
}

// Handle client connection
static void* rtmp_core_handle_connection(void *arg) {
    rtmp_connection_t *conn = (rtmp_connection_t*)arg;
    int handshake_attempts = 0;
    
    // Perform handshake
    while (handshake_attempts < RTMP_RECOVERY_ATTEMPTS) {
        if (rtmp_handshake_perform(conn) == RTMP_SUCCESS) {
            break;
        }
        handshake_attempts++;
        if (handshake_attempts < RTMP_RECOVERY_ATTEMPTS) {
            rtmp_log_warning("Handshake failed, retrying...");
            usleep(1000000); // Wait 1 second before retry
        }
    }
    
    if (handshake_attempts >= RTMP_RECOVERY_ATTEMPTS) {
        rtmp_log_error("Handshake failed after multiple attempts");
        goto cleanup;
    }
    
    // Connection loop
    while (conn->active && server_ctx.running) {
        // Process incoming messages
        int result = rtmp_protocol_process_message(conn);
        if (result != RTMP_SUCCESS) {
            if (server_ctx.config.enable_recovery && rtmp_core_recover_connection(conn) == RTMP_SUCCESS) {
                continue;
            }
            break;
        }
        
        // Handle stream if active
        if (conn->has_stream) {
            result = rtmp_stream_process(conn);
            if (result != RTMP_SUCCESS) {
                if (server_ctx.config.enable_recovery && rtmp_core_recover_connection(conn) == RTMP_SUCCESS) {
                    continue;
                }
                break;
            }
        }
        
        // Update statistics
        pthread_mutex_lock(&server_ctx.stats_mutex);
        server_ctx.stats.bytes_received += conn->bytes_received;
        server_ctx.stats.bytes_sent += conn->bytes_sent;
        pthread_mutex_unlock(&server_ctx.stats_mutex);
        
        // Perform heartbeat check
        if (time(NULL) - conn->last_heartbeat > RTMP_HEARTBEAT_INTERVAL) {
            if (rtmp_protocol_send_ping(conn) != RTMP_SUCCESS) {
                break;
            }
            conn->last_heartbeat = time(NULL);
        }
        
        usleep(1000); // Prevent CPU overload
    }
    
cleanup:
    // Clean up connection
    pthread_mutex_lock(&server_ctx.conn_mutex);
    for (int i = 0; i < server_ctx.active_connections; i++) {
        if (server_ctx.connections[i] == conn) {
            // Shift remaining connections
            for (int j = i; j < server_ctx.active_connections - 1; j++) {
                server_ctx.connections[j] = server_ctx.connections[j + 1];
            }
            server_ctx.active_connections--;
            break;
        }
    }
    pthread_mutex_unlock(&server_ctx.conn_mutex);
    
    // Update statistics
    pthread_mutex_lock(&server_ctx.stats_mutex);
    if (conn->has_stream) {
        server_ctx.stats.active_streams--;
    }
    pthread_mutex_unlock(&server_ctx.stats_mutex);
    
    rtmp_connection_destroy(conn);
    return NULL;
}

// Accept new connections
static void* rtmp_core_accept_loop(void *arg) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    while (server_ctx.running) {
        // Accept new connection
        int client_socket = accept(server_ctx.server_socket, (struct sockaddr*)&client_addr, &addr_len);
        if (client_socket < 0) {
            if (server_ctx.running) {
                rtmp_log_warning("Failed to accept connection");
            }
            continue;
        }
        
        // Check connection limit
        pthread_mutex_lock(&server_ctx.conn_mutex);
        if (server_ctx.active_connections >= server_ctx.config.max_connections) {
            pthread_mutex_unlock(&server_ctx.conn_mutex);
            close(client_socket);
            rtmp_log_warning("Connection limit reached");
            continue;
        }
        
        // Create new connection
        rtmp_connection_t *conn = rtmp_connection_create(client_socket);
        if (!conn) {
            pthread_mutex_unlock(&server_ctx.conn_mutex);
            close(client_socket);
            rtmp_log_error("Failed to create connection");
            continue;
        }
        
        // Store client info
        conn->client_ip = strdup(inet_ntoa(client_addr.sin_addr));
        conn->client_port = ntohs(client_addr.sin_port);
        
        // Start connection thread
        pthread_t conn_thread;
        if (pthread_create(&conn_thread, NULL, rtmp_core_handle_connection, conn) != 0) {
            pthread_mutex_unlock(&server_ctx.conn_mutex);
            rtmp_connection_destroy(conn);
            rtmp_log_error("Failed to create connection thread");
            continue;
        }
        
        // Add to connections array
        server_ctx.connections[server_ctx.active_connections++] = conn;
        
        // Update statistics
        pthread_mutex_lock(&server_ctx.stats_mutex);
        server_ctx.stats.total_connections++;
        pthread_mutex_unlock(&server_ctx.stats_mutex);
        
        pthread_mutex_unlock(&server_ctx.conn_mutex);
        
        rtmp_log_info("New connection from %s:%d", conn->client_ip, conn->client_port);
    }
    
    return NULL;
}

// Monitor server health
static void* rtmp_core_monitor_loop(void *arg) {
    while (server_ctx.running) {
        pthread_mutex_lock(&server_ctx.conn_mutex);
        
        // Check each connection
        for (int i = 0; i < server_ctx.active_connections; i++) {
            rtmp_connection_t *conn = server_ctx.connections[i];
            
            // Check connection health
            if (!rtmp_connection_is_healthy(conn)) {
                if (server_ctx.config.enable_recovery) {
                    if (rtmp_core_recover_connection(conn) != RTMP_SUCCESS) {
                        conn->active = 0;
                    }
                } else {
                    conn->active = 0;
                }
            }
        }
        
        pthread_mutex_unlock(&server_ctx.conn_mutex);
        
        // Update statistics
        pthread_mutex_lock(&server_ctx.stats_mutex);
        server_ctx.stats.uptime = time(NULL) - server_ctx.stats.start_time;
        if (server_ctx.stats.uptime > 0) {
            server_ctx.stats.avg_bandwidth = (server_ctx.stats.bytes_sent + server_ctx.stats.bytes_received) / server_ctx.stats.uptime;
        }
        pthread_mutex_unlock(&server_ctx.stats_mutex);
        
        sleep(1); // Check every second
    }
    
    return NULL;
}

// Attempt to recover a failing connection
static int rtmp_core_recover_connection(rtmp_connection_t *conn) {
    if (!conn || !conn->active) {
        return RTMP_ERROR_INVALID_CONNECTION;
    }
    
    rtmp_log_warning("Attempting to recover connection from %s:%d", conn->client_ip, conn->client_port);
    
    // Reset connection state
    conn->bytes_received = 0;
    conn->bytes_sent = 0;
    conn->last_heartbeat = time(NULL);
    
    // Attempt to re-establish stream if needed
    if (conn->has_stream) {
        if (rtmp_stream_reset(conn) != RTMP_SUCCESS) {
            rtmp_log_error("Failed to reset stream");
            return RTMP_ERROR_STREAM_RESET;
        }
    }
    
    rtmp_log_info("Connection recovered successfully");
    return RTMP_SUCCESS;
}

// Stop the RTMP server
int rtmp_core_stop(void) {
    if (!server_ctx.running) {
        return RTMP_SUCCESS;
    }
    
    server_ctx.running = 0;
    
    // Close server socket
    if (server_ctx.server_socket >= 0) {
        close(server_ctx.server_socket);
    }
    
    // Clean up connections
    pthread_mutex_lock(&server_ctx.conn_mutex);
    for (int i = 0; i < server_ctx.active_connections; i++) {
        rtmp_connection_t *conn = server_ctx.connections[i];
        conn->active = 0;
        rtmp_connection_destroy(conn);
    }
    server_ctx.active_connections = 0;
    pthread_mutex_unlock(&server_ctx.conn_mutex);
    
    // Destroy mutexes
    pthread_mutex_destroy(&server_ctx.conn_mutex);
    pthread_mutex_destroy(&server_ctx.stats_mutex);
    
    rtmp_log_info("RTMP Server stopped");
	// Final cleanup
    free(server_ctx.config.cert_file);
    free(server_ctx.config.key_file);
    
    return RTMP_SUCCESS;
}

// Get server statistics
const rtmp_stats_t* rtmp_core_get_stats(void) {
    const rtmp_stats_t *stats = NULL;
    
    pthread_mutex_lock(&server_ctx.stats_mutex);
    stats = &server_ctx.stats;
    pthread_mutex_unlock(&server_ctx.stats_mutex);
    
    return stats;
}

// Connection management functions
rtmp_connection_t* rtmp_connection_create(int socket) {
    rtmp_connection_t *conn = (rtmp_connection_t*)calloc(1, sizeof(rtmp_connection_t));
    if (!conn) {
        rtmp_log_error("Failed to allocate connection");
        return NULL;
    }
    
    conn->socket = socket;
    conn->active = 1;
    conn->last_heartbeat = time(NULL);
    conn->has_stream = 0;
    conn->bytes_received = 0;
    conn->bytes_sent = 0;
    
    return conn;
}

void rtmp_connection_destroy(rtmp_connection_t *conn) {
    if (!conn) {
        return;
    }
    
    if (conn->socket >= 0) {
        close(conn->socket);
    }
    
    if (conn->has_stream && conn->stream_data) {
        rtmp_stream_cleanup(conn->stream_data);
    }
    
    free(conn->client_ip);
    free(conn->user_data);
    free(conn);
}

int rtmp_connection_is_healthy(const rtmp_connection_t *conn) {
    if (!conn || !conn->active) {
        return 0;
    }
    
    // Check socket validity
    if (conn->socket < 0) {
        return 0;
    }
    
    // Check heartbeat timeout
    if (time(NULL) - conn->last_heartbeat > RTMP_HEARTBEAT_INTERVAL * 2) {
        return 0;
    }
    
    // Check stream health if active
    if (conn->has_stream && !rtmp_stream_is_healthy(conn->stream_data)) {
        return 0;
    }
    
    return 1;
}

// Configuration functions
int rtmp_core_set_config(const rtmp_config_t *config) {
    if (!config) {
        return RTMP_ERROR_INVALID_PARAM;
    }
    
    pthread_mutex_lock(&server_ctx.conn_mutex);
    
    // Update configuration
    server_ctx.config.port = config->port > 0 ? config->port : RTMP_DEFAULT_PORT;
    server_ctx.config.max_connections = config->max_connections > 0 ? 
        config->max_connections : RTMP_MAX_CONNECTIONS;
    server_ctx.config.buffer_size = config->buffer_size > 0 ? 
        config->buffer_size : RTMP_BUFFER_SIZE;
    server_ctx.config.enable_recovery = config->enable_recovery;
    server_ctx.config.log_level = config->log_level;
    
    // Update SSL configuration if provided
    if (config->cert_file) {
        free(server_ctx.config.cert_file);
        server_ctx.config.cert_file = strdup(config->cert_file);
    }
    
    if (config->key_file) {
        free(server_ctx.config.key_file);
        server_ctx.config.key_file = strdup(config->key_file);
    }
    
    pthread_mutex_unlock(&server_ctx.conn_mutex);
    
    return RTMP_SUCCESS;
}

// Advanced stream control
int rtmp_core_force_keyframe(rtmp_connection_t *conn) {
    if (!conn || !conn->has_stream) {
        return RTMP_ERROR_INVALID_CONNECTION;
    }
    
    return rtmp_stream_request_keyframe(conn->stream_data);
}

int rtmp_core_adjust_quality(rtmp_connection_t *conn, int quality) {
    if (!conn || !conn->has_stream) {
        return RTMP_ERROR_INVALID_CONNECTION;
    }
    
    return rtmp_stream_set_quality(conn->stream_data, quality);
}

// Debug and diagnostics
void rtmp_core_dump_stats(void) {
    pthread_mutex_lock(&server_ctx.stats_mutex);
    
    rtmp_log_info("=== RTMP Server Statistics ===");
    rtmp_log_info("Uptime: %lu seconds", server_ctx.stats.uptime);
    rtmp_log_info("Total Connections: %lu", server_ctx.stats.total_connections);
    rtmp_log_info("Active Streams: %u", server_ctx.stats.active_streams);
    rtmp_log_info("Bytes Received: %lu", server_ctx.stats.bytes_received);
    rtmp_log_info("Bytes Sent: %lu", server_ctx.stats.bytes_sent);
    rtmp_log_info("Average Bandwidth: %.2f KB/s", server_ctx.stats.avg_bandwidth / 1024.0);
    rtmp_log_info("Dropped Frames: %lu", server_ctx.stats.dropped_frames);
    
    pthread_mutex_unlock(&server_ctx.stats_mutex);
}

// Emergency shutdown handling
static void rtmp_core_emergency_shutdown(void) {
    rtmp_log_error("Emergency shutdown initiated");
    
    // Force stop all connections
    pthread_mutex_lock(&server_ctx.conn_mutex);
    for (int i = 0; i < server_ctx.active_connections; i++) {
        rtmp_connection_t *conn = server_ctx.connections[i];
        conn->active = 0;
        close(conn->socket);
    }
    pthread_mutex_unlock(&server_ctx.conn_mutex);
    
    // Close server socket
    if (server_ctx.server_socket >= 0) {
        close(server_ctx.server_socket);
    }
    
    server_ctx.running = 0;
}