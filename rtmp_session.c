#include "rtmp_session.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>

// Internal session structure
struct rtmp_session_t {
    rtmp_session_config_t config;
    rtmp_session_callbacks_t callbacks;
    rtmp_session_state_t state;
    rtmp_session_stats_t stats;
    
    rtmp_context_t *rtmp;
    uint32_t stream_id;
    uint32_t transaction_id;
    uint64_t session_start_time;
    uint64_t last_ping_time;
    uint64_t last_activity_time;
    
    char error_message[256];
    bool is_publisher;
    bool is_playing;
    
    // Buffer management
    uint8_t *temp_buffer;
    size_t temp_buffer_size;
    
    // Metadata
    char *metadata_name;
    uint8_t *metadata;
    size_t metadata_size;
};

// Internal utility functions
static uint64_t get_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

static void rtmp_session_handle_error(rtmp_session_t *session, const char *fmt, ...) {
    if (!session) return;
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(session->error_message, sizeof(session->error_message), fmt, args);
    va_end(args);
    
    if (session->callbacks.on_error) {
        session->callbacks.on_error(session, session->error_message);
    }
}

static void rtmp_session_update_state(rtmp_session_t *session, rtmp_session_state_t new_state) {
    if (!session || session->state == new_state) return;
    
    rtmp_session_state_t old_state = session->state;
    session->state = new_state;
    
    if (session->callbacks.on_state_change) {
        session->callbacks.on_state_change(session, old_state, new_state);
    }
}

rtmp_session_t* rtmp_session_create(const rtmp_session_config_t *config, const rtmp_session_callbacks_t *callbacks) {
    if (!config) return NULL;
    
    rtmp_session_t *session = (rtmp_session_t*)calloc(1, sizeof(rtmp_session_t));
    if (!session) return NULL;
    
    // Copy configuration
    session->config = *config;
    
    // Set callbacks if provided
    if (callbacks) {
        session->callbacks = *callbacks;
    }
    
    // Initialize RTMP context
    rtmp_config_t rtmp_config = {
        .chunk_size = config->chunk_size,
        .window_size = 2500000,
        .peer_bandwidth = 2500000,
        .peer_bandwidth_limit_type = 2,
        .tcp_nodelay = true,
        .timeout_ms = config->timeout_ms
    };
    
    rtmp_callbacks_t rtmp_callbacks = {
        .on_state_change = NULL,  // Will be set internally
        .on_chunk_received = NULL,
        .on_message_received = NULL,
        .on_error = NULL
    };
    
    session->rtmp = rtmp_create(&rtmp_config, &rtmp_callbacks);
    if (!session->rtmp) {
        free(session);
        return NULL;
    }
    
    // Initialize other members
    session->state = RTMP_SESSION_STATE_NEW;
    session->stream_id = 0;
    session->transaction_id = 1;
    session->session_start_time = get_current_time_ms();
    session->last_ping_time = session->session_start_time;
    session->last_activity_time = session->session_start_time;
    
    // Allocate temporary buffer
    session->temp_buffer_size = 64 * 1024;  // 64KB initial size
    session->temp_buffer = (uint8_t*)malloc(session->temp_buffer_size);
    if (!session->temp_buffer) {
        rtmp_destroy(session->rtmp);
        free(session);
        return NULL;
    }
    
    return session;
}

void rtmp_session_destroy(rtmp_session_t *session) {
    if (!session) return;
    
    // Stop session if still active
    if (session->state != RTMP_SESSION_STATE_CLOSED) {
        rtmp_session_stop(session);
    }
    
    // Cleanup RTMP context
    if (session->rtmp) {
        rtmp_destroy(session->rtmp);
    }
    
    // Free buffers
    free(session->temp_buffer);
    free(session->metadata_name);
    free(session->metadata);
    
    free(session);
}

rtmp_error_t rtmp_session_start(rtmp_session_t *session) {
    if (!session) return RTMP_ERROR_INVALID_STATE;
    
    // Update state
    rtmp_session_update_state(session, RTMP_SESSION_STATE_HANDSHAKING);
    
    // Start RTMP connection
    rtmp_error_t err = rtmp_connect(session->rtmp, session->config.app_name, RTMP_DEFAULT_PORT);
    if (err != RTMP_ERROR_OK) {
        rtmp_session_handle_error(session, "Failed to connect: %s", rtmp_get_error_string(err));
        rtmp_session_update_state(session, RTMP_SESSION_STATE_ERROR);
        return err;
    }
    
    // Start publisher or player
    if (session->config.is_publisher) {
        err = rtmp_session_start_publish(session);
    } else {
        err = rtmp_session_start_play(session);
    }
    
    if (err != RTMP_ERROR_OK) {
        rtmp_session_update_state(session, RTMP_SESSION_STATE_ERROR);
        return err;
    }
    
    return RTMP_ERROR_OK;
}

rtmp_error_t rtmp_session_stop(rtmp_session_t *session) {
    if (!session) return RTMP_ERROR_INVALID_STATE;
    
    rtmp_session_update_state(session, RTMP_SESSION_STATE_CLOSING);
    
    // Stop publishing/playing
    if (session->is_publisher) {
        rtmp_session_stop_publish(session);
    } else if (session->is_playing) {
        rtmp_session_stop_play(session);
    }
    
    // Disconnect RTMP
    rtmp_error_t err = rtmp_disconnect(session->rtmp);
    if (err != RTMP_ERROR_OK) {
        rtmp_session_handle_error(session, "Failed to disconnect: %s", rtmp_get_error_string(err));
        rtmp_session_update_state(session, RTMP_SESSION_STATE_ERROR);
        return err;
    }
    
    rtmp_session_update_state(session, RTMP_SESSION_STATE_CLOSED);
    return RTMP_ERROR_OK;
}

rtmp_error_t rtmp_session_send_audio(rtmp_session_t *session, const uint8_t *data, size_t len, uint32_t timestamp) {
    if (!session || !data || !len) return RTMP_ERROR_INVALID_STATE;
    if (!session->is_publisher) return RTMP_ERROR_INVALID_STATE;
    
    rtmp_error_t err = rtmp_send_audio(session->rtmp, data, len, timestamp);
    if (err == RTMP_ERROR_OK) {
        session->stats.bytes_sent += len;
        session->stats.messages_sent++;
        session->last_activity_time = get_current_time_ms();
    }
    
    return err;
}

rtmp_error_t rtmp_session_send_video(rtmp_session_t *session, const uint8_t *data, size_t len, uint32_t timestamp) {
    if (!session || !data || !len) return RTMP_ERROR_INVALID_STATE;
    if (!session->is_publisher) return RTMP_ERROR_INVALID_STATE;
    
    rtmp_error_t err = rtmp_send_video(session->rtmp, data, len, timestamp);
    if (err == RTMP_ERROR_OK) {
        session->stats.bytes_sent += len;
        session->stats.messages_sent++;
        session->last_activity_time = get_current_time_ms();
    }
    
    return err;
}

rtmp_error_t rtmp_session_send_metadata(rtmp_session_t *session, const char *name, const uint8_t *data, size_t len) {
    if (!session || !name || !data || !len) return RTMP_ERROR_INVALID_STATE;
    if (!session->is_publisher) return RTMP_ERROR_INVALID_STATE;
    
    // Update stored metadata
    free(session->metadata_name);
    free(session->metadata);
    
    session->metadata_name = strdup(name);
    session->metadata = malloc(len);
    if (!session->metadata_name || !session->metadata) {
        free(session->metadata_name);
        free(session->metadata);
        session->metadata_name = NULL;
        session->metadata = NULL;
        session->metadata_size = 0;
        return RTMP_ERROR_MEMORY;
    }
    
    memcpy(session->metadata, data, len);
    session->metadata_size = len;
    
    // Send metadata message
    rtmp_error_t err = rtmp_send_message(session->rtmp, 
                                        RTMP_MSG_DATA_AMF0, 
                                        session->stream_id,
                                        data, len);
    
    if (err == RTMP_ERROR_OK) {
        session->stats.bytes_sent += len;
        session->stats.messages_sent++;
        session->last_activity_time = get_current_time_ms();
    }
    
    return err;
}

rtmp_error_t rtmp_session_ping(rtmp_session_t *session) {
    if (!session) return RTMP_ERROR_INVALID_STATE;
    
    uint64_t current_time = get_current_time_ms();
    
    // Check if it's time to send ping
    if (current_time - session->last_ping_time >= session->config.ping_interval) {
        uint8_t ping_data[6];
        uint16_t type = htons(RTMP_USER_PING_REQUEST);
        uint32_t timestamp = htonl((uint32_t)current_time);
        
        memcpy(ping_data, &type, 2);
        memcpy(ping_data + 2, &timestamp, 4);
        
        rtmp_error_t err = rtmp_send_message(session->rtmp, 
                                          RTMP_MSG_USER_CONTROL,
                                          0, ping_data, sizeof(ping_data));
        
        if (err == RTMP_ERROR_OK) {
            session->last_ping_time = current_time;
        }
        
        return err;
    }
    
    return RTMP_ERROR_OK;
}

rtmp_error_t rtmp_session_process(rtmp_session_t *session) {
    if (!session) return RTMP_ERROR_INVALID_STATE;
    
    // Process RTMP messages
    rtmp_error_t err = RTMP_ERROR_OK;
    while (err == RTMP_ERROR_OK) {
        rtmp_chunk_t *chunk = rtmp_chunk_create();
        if (!chunk) return RTMP_ERROR_MEMORY;
        
        err = rtmp_receive_chunk(session->rtmp, chunk);
        if (err == RTMP_ERROR_OK) {
            err = rtmp_session_handle_message(session, chunk);
        }
        
        rtmp_chunk_destroy(chunk);
    }
    
    // Check for timeout
    uint64_t current_time = get_current_time_ms();
    if (current_time - session->last_activity_time > session->config.timeout_ms) {
        rtmp_session_handle_error(session, "Session timeout");
        return RTMP_ERROR_TIMEOUT;
    }
    
    // Send periodic ping if needed
    err = rtmp_session_ping(session);
    if (err != RTMP_ERROR_OK) {
        rtmp_session_handle_error(session, "Failed to send ping");
        return err;
    }
    
    return RTMP_ERROR_OK;
}

bool rtmp_session_is_connected(const rtmp_session_t *session) {
    if (!session) return false;
    return session->state == RTMP_SESSION_STATE_CONNECTED ||
           session->state == RTMP_SESSION_STATE_PUBLISHING ||
           session->state == RTMP_SESSION_STATE_PLAYING;
}

bool rtmp_session_is_active(const rtmp_session_t *session) {
    if (!session) return false;
    return session->state != RTMP_SESSION_STATE_CLOSED &&
           session->state != RTMP_SESSION_STATE_ERROR;
}

rtmp_session_state_t rtmp_session_get_state(const rtmp_session_t *session) {
    return session ? session->state : RTMP_SESSION_STATE_ERROR;
}

const rtmp_session_stats_t* rtmp_session_get_stats(const rtmp_session_t *session) {
    return session ? &session->stats : NULL;
}

const char* rtmp_session_get_error(const rtmp_session_t *session) {
    return session ? session->error_message : "Invalid session";
}

void rtmp_session_set_chunk_size(rtmp_session_t *session, uint32_t size) {
    if (session && size > 0) {
        rtmp_set_chunk_size(session->rtmp, size);
    }
}

void rtmp_session_set_buffer_time(rtmp_session_t *session, uint32_t ms) {
    if (session) {
        uint8_t buffer_data[10];
        uint16_t type = htons(RTMP_USER_SET_BUFFER_LEN);
        uint32_t stream_id = htonl(session->stream_id);
        uint32_t buffer_time = htonl(ms);
        
        memcpy(buffer_data, &type, 2);
        memcpy(buffer_data + 2, &stream_id, 4);
        memcpy(buffer_data + 6, &buffer_time, 4);
        
        rtmp_send_message(session->rtmp, RTMP_MSG_USER_CONTROL, 0, buffer_data, sizeof(buffer_data));
    }
}

void rtmp_session_set_stream_id(rtmp_session_t *session, uint32_t stream_id) {
    if (session) {
        session->stream_id = stream_id;
    }
}