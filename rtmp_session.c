#include "rtmp_session.h"
#include "rtmp_protocol.h"
#include "rtmp_utils.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

rtmp_session_t* rtmp_session_create(int socket_fd) {
    rtmp_session_t *session = (rtmp_session_t*)calloc(1, sizeof(rtmp_session_t));
    if (!session) return NULL;
    
    session->socket_fd = socket_fd;
    session->state = RTMP_SESSION_STATE_INIT;
    session->in_chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
    session->out_chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
    session->window_ack_size = RTMP_DEFAULT_WINDOW_SIZE;
    session->peer_bandwidth = RTMP_DEFAULT_WINDOW_SIZE;
    
    // Initialize chunk streams array
    memset(session->chunk_streams, 0, sizeof(session->chunk_streams));
    
    return session;
}

void rtmp_session_destroy(rtmp_session_t *session) {
    if (!session) return;
    
    // Close socket
    if (session->socket_fd >= 0) {
        close(session->socket_fd);
    }
    
    // Free chunk streams
    for (int i = 0; i < RTMP_MAX_CHUNK_STREAMS; i++) {
        if (session->chunk_streams[i]) {
            rtmp_free_chunk_stream(session->chunk_streams[i]);
        }
    }
    
    // Free stream name
    if (session->stream_name) {
        free(session->stream_name);
    }
    
    // Free sequence headers
    if (session->aac_sequence_header) {
        free(session->aac_sequence_header);
    }
    if (session->avc_sequence_header) {
        free(session->avc_sequence_header);
    }
    
    free(session);
}

int rtmp_session_send_data(rtmp_session_t *session, const uint8_t *data, size_t size) {
    if (!session || !data || !size) return -1;
    
    size_t total_sent = 0;
    while (total_sent < size) {
        ssize_t sent = send(session->socket_fd, data + total_sent, size - total_sent, 0);
        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full, wait a bit
                usleep(1000);
                continue;
            }
            return -1;
        }
        total_sent += sent;
    }
    
    return 0;
}

int rtmp_session_close(rtmp_session_t *session) {
    if (!session) return -1;
    
    session->state = RTMP_SESSION_STATE_CLOSING;
    
    // Send any pending data
    // TODO: Implement flush mechanism if needed
    
    shutdown(session->socket_fd, SHUT_RDWR);
    close(session->socket_fd);
    session->socket_fd = -1;
    
    session->state = RTMP_SESSION_STATE_CLOSED;
    return 0;
}

// Stream management functions
uint32_t rtmp_session_create_stream(rtmp_session_t *session) {
    if (!session) return 0;
    
    static uint32_t next_stream_id = 1;
    
    // Simple stream ID generation
    uint32_t stream_id = next_stream_id++;
    if (next_stream_id == 0) next_stream_id = 1;
    
    session->stream_id = stream_id;
    return stream_id;
}

int rtmp_session_delete_stream(rtmp_session_t *session, uint32_t stream_id) {
    if (!session) return -1;
    
    if (session->stream_id == stream_id) {
        // Clear stream state
        session->stream_id = 0;
        session->is_publishing = 0;
        
        if (session->stream_name) {
            free(session->stream_name);
            session->stream_name = NULL;
        }
    }
    
    return 0;
}

int rtmp_session_set_publish_stream(rtmp_session_t *session, const char *stream_name) {
    if (!session || !stream_name) return -1;
    
    // Check if already publishing
    if (session->is_publishing) {
        return -1;
    }
    
    // Store stream name
    char *new_name = strdup(stream_name);
    if (!new_name) return -1;
    
    if (session->stream_name) {
        free(session->stream_name);
    }
    session->stream_name = new_name;
    session->is_publishing = 1;
    
    return 0;
}

int rtmp_session_set_play_stream(rtmp_session_t *session, const char *stream_name) {
    if (!session || !stream_name) return -1;
    
    // Check if already publishing
    if (session->is_publishing) {
        return -1;
    }
    
    // Store stream name
    char *new_name = strdup(stream_name);
    if (!new_name) return -1;
    
    if (session->stream_name) {
        free(session->stream_name);
    }
    session->stream_name = new_name;
    session->is_publishing = 0;
    
    return 0;
}

// Chunk stream management
rtmp_chunk_stream_t* rtmp_get_chunk_stream(rtmp_session_t *session, uint32_t chunk_stream_id) {
    if (!session || chunk_stream_id >= RTMP_MAX_CHUNK_STREAMS) return NULL;
    
    if (!session->chunk_streams[chunk_stream_id]) {
        // Create new chunk stream
        rtmp_chunk_stream_t *chunk_stream = (rtmp_chunk_stream_t*)calloc(1, sizeof(rtmp_chunk_stream_t));
        if (!chunk_stream) return NULL;
        
        session->chunk_streams[chunk_stream_id] = chunk_stream;
    }
    
    return session->chunk_streams[chunk_stream_id];
}

void rtmp_free_chunk_stream(rtmp_chunk_stream_t *chunk_stream) {
    if (!chunk_stream) return;
    
    if (chunk_stream->msg_data) {
        free(chunk_stream->msg_data);
    }
    
    free(chunk_stream);
}

// Buffer management
int rtmp_session_handle_acknowledgement(rtmp_session_t *session) {
    if (!session) return -1;
    
    if (session->bytes_received >= session->window_ack_size) {
        // Send acknowledgement
        uint32_t ack_size = session->bytes_received;
        return rtmp_send_message(session, RTMP_MSG_ACKNOWLEDGEMENT, 0, 
                               (uint8_t*)&ack_size, sizeof(ack_size));
    }
    
    return 0;
}

int rtmp_session_update_bytes_received(rtmp_session_t *session, size_t bytes) {
    if (!session) return -1;
    
    session->bytes_received += bytes;
    return rtmp_session_handle_acknowledgement(session);
}

// Media handling functions
int rtmp_session_send_video(rtmp_session_t *session, const uint8_t *data, size_t size, uint32_t timestamp) {
    if (!session || !data || !size) return -1;
    
    // Create video message
    uint8_t *message = (uint8_t*)malloc(size + 5);
    if (!message) return -1;
    
    // FLV video tag header
    message[0] = 0x17; // KeyFrame(1) + AVC(7)
    message[1] = 0x01; // AVC NALU
    message[2] = 0x00; // Composition time offset
    message[3] = 0x00;
    message[4] = 0x00;
    
    // Copy video data
    memcpy(message + 5, data, size);
    
    // Send message
    int result = rtmp_send_message(session, RTMP_MSG_VIDEO, session->stream_id, 
                                 message, size + 5);
    
    free(message);
    return result;
}

int rtmp_session_send_audio(rtmp_session_t *session, const uint8_t *data, size_t size, uint32_t timestamp) {
    if (!session || !data || !size) return -1;
    
    // Create audio message
    uint8_t *message = (uint8_t*)malloc(size + 2);
    if (!message) return -1;
    
    // FLV audio tag header
    message[0] = 0xaf; // AAC(10) + 44kHz(3) + 16bit(1) + Stereo(1)
    message[1] = 0x01; // AAC raw data
    
    // Copy audio data
    memcpy(message + 2, data, size);
    
    // Send message
    int result = rtmp_send_message(session, RTMP_MSG_AUDIO, session->stream_id, 
                                 message, size + 2);
    
    free(message);
    return result;
}

int rtmp_session_send_metadata(rtmp_session_t *session, const uint8_t *data, size_t size) {
    if (!session || !data || !size) return -1;
    
    rtmp_amf_t *amf = rtmp_amf_create();
    if (!amf) return -1;
    
    // @setDataFrame marker
    rtmp_amf_encode_string(amf, "@setDataFrame");
    
    // onMetaData event
    rtmp_amf_encode_string(amf, "onMetaData");
    
    // Copy metadata
    size_t amf_size;
    const uint8_t *amf_data = rtmp_amf_get_data(amf, &amf_size);
    
    uint8_t *message = (uint8_t*)malloc(amf_size + size);
    if (!message) {
        rtmp_amf_destroy(amf);
        return -1;
    }
    
    memcpy(message, amf_data, amf_size);
    memcpy(message + amf_size, data, size);
    
    // Send message
    int result = rtmp_send_message(session, RTMP_MSG_DATA_AMF0, session->stream_id, 
                                 message, amf_size + size);
    
    free(message);
    rtmp_amf_destroy(amf);
    return result;
}

// State management
int rtmp_session_set_state(rtmp_session_t *session, int state) {
    if (!session) return -1;
    
    if (state < RTMP_SESSION_STATE_INIT || state > RTMP_SESSION_STATE_CLOSED) {
        return -1;
    }
    
    session->state = state;
    return 0;
}

int rtmp_session_get_state(rtmp_session_t *session) {
    if (!session) return -1;
    return session->state;
}