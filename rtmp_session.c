#include "rtmp_session.h"
#include "rtmp_log.h"
#include "rtmp_preview.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t chunk_size;
    uint32_t stream_id;
    uint32_t timestamp;
    uint8_t type;
    uint8_t* data;
    size_t data_size;
} RTMPMessage;

struct RTMPSession {
    int socket;
    uint32_t chunk_size;
    uint32_t bandwidth;
    uint32_t window_size;
    RTMPMessage* messages[64];
    uint8_t state;
    char app_name[128];
    char stream_name[128];
};

RTMPSession* rtmp_session_create(void) {
    RTMPSession* session = calloc(1, sizeof(RTMPSession));
    if (!session) {
        LOG_ERROR("Failed to allocate session");
        return NULL;
    }
    
    session->chunk_size = 128;
    session->bandwidth = 2500000;
    session->window_size = 2500000;
    
    LOG_INFO("Created new RTMP session");
    return session;
}

void rtmp_session_destroy(RTMPSession* session) {
    if (!session) return;
    
    for (int i = 0; i < 64; i++) {
        if (session->messages[i]) {
            free(session->messages[i]->data);
            free(session->messages[i]);
        }
    }
    
    free(session);
    LOG_INFO("Destroyed RTMP session");
}

static int handle_video_data(RTMPSession* session, RTMPChunk* chunk) {
    if (!chunk->data || chunk->length == 0) return -1;
    
    uint8_t codec_id = chunk->data[0] & 0x0F;
    
    if (codec_id != 7) {
        LOG_WARNING("Unsupported video codec: %d", codec_id);
        return 0;
    }
    
    uint8_t avc_packet_type = chunk->data[1];
    uint8_t* video_data = chunk->data + 5;
    size_t video_length = chunk->length - 5;
    
    switch (avc_packet_type) {
        case 0:
            LOG_INFO("Received AVC sequence header");
            break;
            
        case 1:
            rtmp_preview_process_video(video_data, video_length, chunk->timestamp);
            break;
            
        case 2:
            LOG_INFO("Received AVC end of sequence");
            break;
    }
    
    return 0;
}

static int handle_audio_data(RTMPSession* session, RTMPChunk* chunk) {
    if (!chunk->data || chunk->length == 0) return -1;
    
    uint8_t format = (chunk->data[0] & 0xF0) >> 4;
    
    if (format != 10) {
        LOG_WARNING("Unsupported audio format: %d", format);
        return 0;
    }
    
    uint8_t aac_packet_type = chunk->data[1];
    uint8_t* audio_data = chunk->data + 2;
    size_t audio_length = chunk->length - 2;
    
    switch (aac_packet_type) {
        case 0:
            LOG_INFO("Received AAC sequence header");
            break;
            
        case 1:
            rtmp_preview_process_audio(audio_data, audio_length, chunk->timestamp);
            break;
            
        default:
            LOG_WARNING("Unknown AAC packet type: %d", aac_packet_type);
            break;
    }
    
    return 0;
}

int rtmp_session_process_chunk(RTMPSession* session, RTMPChunk* chunk) {
    if (!session || !chunk) return -1;
    
    switch (chunk->type) {
        case 8:  // Audio Data
            return handle_audio_data(session, chunk);
            
        case 9:  // Video Data
            return handle_video_data(session, chunk);
            
        case 20: // AMF0 Command
            LOG_DEBUG("Received AMF0 command");
            break;
            
        default:
            LOG_DEBUG("Unhandled message type: %d", chunk->type);
            break;
    }
    
    return 0;
}

int rtmp_session_send_chunk(RTMPSession* session, RTMPChunk* chunk) {
    if (!session || !chunk) return -1;
    return 0;
}

int rtmp_session_handle_connect(RTMPSession* session, const uint8_t* data, size_t len) {
    LOG_INFO("Handling connect command");
    return 0;
}

int rtmp_session_handle_createStream(RTMPSession* session, const uint8_t* data, size_t len) {
    LOG_INFO("Handling createStream command");
    return 0;
}

int rtmp_session_handle_publish(RTMPSession* session, const uint8_t* data, size_t len) {
    LOG_INFO("Handling publish command");
    rtmp_preview_show();
    return 0;
}

int rtmp_session_handle_play(RTMPSession* session, const uint8_t* data, size_t len) {
    LOG_INFO("Handling play command");
    return 0;
}