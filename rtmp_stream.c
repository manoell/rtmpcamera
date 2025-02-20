#include "rtmp_stream.h"
#include "rtmp_log.h"
#include "rtmp_preview.h"
#include <stdlib.h>
#include <string.h>

// Estrutura de buffer circular para vídeo
#define VIDEO_BUFFER_SIZE 10
typedef struct {
    uint8_t* data;
    size_t length;
    uint32_t timestamp;
} VideoFrame;

struct RTMPStream {
    char name[128];
    int active;
    
    // Callbacks
    rtmp_video_callback video_cb;
    rtmp_audio_callback audio_cb;
    void* video_ctx;
    void* audio_ctx;
    
    // Video state
    uint8_t* sps;
    size_t sps_size;
    uint8_t* pps;
    size_t pps_size;
    int has_video_config;
    
    // Audio state
    uint8_t* aac_config;
    size_t aac_config_size;
    int has_audio_config;
    
    // Buffer circular de vídeo
    VideoFrame video_buffer[VIDEO_BUFFER_SIZE];
    int video_buffer_write;
    int video_buffer_read;
    pthread_mutex_t video_mutex;
};

RTMPStream* rtmp_stream_create(void) {
    RTMPStream* stream = calloc(1, sizeof(RTMPStream));
    if (!stream) {
        LOG_ERROR("Failed to allocate stream");
        return NULL;
    }
    
    // Inicializar mutex
    if (pthread_mutex_init(&stream->video_mutex, NULL) != 0) {
        LOG_ERROR("Failed to initialize video mutex");
        free(stream);
        return NULL;
    }
    
    LOG_INFO("Created new RTMP stream");
    return stream;
}

void rtmp_stream_destroy(RTMPStream* stream) {
    if (!stream) return;
    
    // Limpar buffers de vídeo
    pthread_mutex_lock(&stream->video_mutex);
    for (int i = 0; i < VIDEO_BUFFER_SIZE; i++) {
        free(stream->video_buffer[i].data);
    }
    pthread_mutex_unlock(&stream->video_mutex);
    
    // Destruir mutex
    pthread_mutex_destroy(&stream->video_mutex);
    
    // Limpar configurações
    free(stream->sps);
    free(stream->pps);
    free(stream->aac_config);
    
    free(stream);
    LOG_INFO("Destroyed RTMP stream");
}

static int handle_video_config(RTMPStream* stream, const uint8_t* data, size_t len) {
    if (len < 10) return -1;
    
    // Skip version and profile
    data += 5;
    len -= 5;
    
    // Skip NAL length size
    data++;
    len--;
    
    // Parse SPS
    uint8_t num_sps = data[0] & 0x1F;
    data++;
    len--;
    
    if (num_sps > 0 && len >= 2) {
        uint16_t sps_size = (data[0] << 8) | data[1];
        data += 2;
        len -= 2;
        
        if (len >= sps_size) {
            free(stream->sps);
            stream->sps = malloc(sps_size);
            if (stream->sps) {
                memcpy(stream->sps, data, sps_size);
                stream->sps_size = sps_size;
            }
            data += sps_size;
            len -= sps_size;
        }
    }
    
    // Parse PPS
    if (len >= 1) {
        uint8_t num_pps = data[0];
        data++;
        len--;
        
        if (num_pps > 0 && len >= 2) {
            uint16_t pps_size = (data[0] << 8) | data[1];
            data += 2;
            len -= 2;
            
            if (len >= pps_size) {
                free(stream->pps);
                stream->pps = malloc(pps_size);
                if (stream->pps) {
                    memcpy(stream->pps, data, pps_size);
                    stream->pps_size = pps_size;
                }
            }
        }
    }
    
    stream->has_video_config = 1;
    LOG_INFO("Video configuration received: SPS=%zu bytes, PPS=%zu bytes",
             stream->sps_size, stream->pps_size);
    
    return 0;
}

static int handle_video_frame(RTMPStream* stream, const uint8_t* data, size_t len, uint32_t timestamp) {
    pthread_mutex_lock(&stream->video_mutex);
    
    // Armazenar frame no buffer circular
    int write_pos = stream->video_buffer_write;
    free(stream->video_buffer[write_pos].data);
    
    stream->video_buffer[write_pos].data = malloc(len);
    if (stream->video_buffer[write_pos].data) {
        memcpy(stream->video_buffer[write_pos].data, data, len);
        stream->video_buffer[write_pos].length = len;
        stream->video_buffer[write_pos].timestamp = timestamp;
        
        stream->video_buffer_write = (write_pos + 1) % VIDEO_BUFFER_SIZE;
    }
    
    pthread_mutex_unlock(&stream->video_mutex);
    
    // Enviar para o preview
    rtmp_preview_process_video(data, len, timestamp);
    
    // Callback de vídeo
    if (stream->video_cb) {
        stream->video_cb(stream->video_ctx, data, len, timestamp);
    }
    
    return 0;
}

int rtmp_stream_process_video(RTMPStream* stream, const uint8_t* data, size_t len, uint32_t timestamp) {
    if (!stream || !data || len < 2) return -1;
    
    uint8_t frame_type = (data[0] & 0xF0) >> 4;
    uint8_t codec_id = data[0] & 0x0F;
    
    // Apenas processar H.264
    if (codec_id != 7) {
        LOG_WARNING("Unsupported video codec: %d", codec_id);
        return 0;
    }
    
    uint8_t avc_packet_type = data[1];
    data += 2;
    len -= 2;
    
    switch (avc_packet_type) {
        case 0: // Sequence header
            return handle_video_config(stream, data, len);
            
        case 1: // NALU
            if (!stream->has_video_config) {
                LOG_WARNING("Received video frame before configuration");
                return 0;
            }
            return handle_video_frame(stream, data, len, timestamp);
            
        case 2: // End of sequence
            LOG_INFO("End of video sequence");
            return 0;
            
        default:
            LOG_WARNING("Unknown AVC packet type: %d", avc_packet_type);
            return -1;
    }
}

static int handle_audio_config(RTMPStream* stream, const uint8_t* data, size_t len) {
    free(stream->aac_config);
    stream->aac_config = malloc(len);
    if (!stream->aac_config) return -1;
    
    memcpy(stream->aac_config, data, len);
    stream->aac_config_size = len;
    stream->has_audio_config = 1;
    
    LOG_INFO("Audio configuration received: %zu bytes", len);
    return 0;
}

int rtmp_stream_process_audio(RTMPStream* stream, const uint8_t* data, size_t len, uint32_t timestamp) {
    if (!stream || !data || len < 2) return -1;
    
    uint8_t format = (data[0] & 0xF0) >> 4;
    uint8_t rate = (data[0] & 0x0C) >> 2;
    uint8_t size = (data[0] & 0x02) >> 1;
    uint8_t type = data[0] & 0x01;
    
    // Apenas processar AAC
    if (format != 10) {
        LOG_WARNING("Unsupported audio format: %d", format);
        return 0;
    }
    
    uint8_t aac_packet_type = data[1];
    data += 2;
    len -= 2;
    
    switch (aac_packet_type) {
        case 0: // AAC sequence header
            return handle_audio_config(stream, data, len);
            
        case 1: // AAC raw
            if (!stream->has_audio_config) {
                LOG_WARNING("Received audio frame before configuration");
                return 0;
            }
            
            rtmp_preview_process_audio(data, len, timestamp);
            
            if (stream->audio_cb) {
                stream->audio_cb(stream->audio_ctx, data, len, timestamp);
            }
            return 0;
            
        default:
            LOG_WARNING("Unknown AAC packet type: %d", aac_packet_type);
            return -1;
    }
}

void rtmp_stream_set_video_callback(RTMPStream* stream, rtmp_video_callback cb, void* ctx) {
    if (!stream) return;
    stream->video_cb = cb;
    stream->video_ctx = ctx;
}

void rtmp_stream_set_audio_callback(RTMPStream* stream, rtmp_audio_callback cb, void* ctx) {
    if (!stream) return;
    stream->audio_cb = cb;
    stream->audio_ctx = ctx;
}

int rtmp_stream_start(RTMPStream* stream, const char* name) {
    if (!stream || !name) return -1;
    
    strncpy(stream->name, name, sizeof(stream->name) - 1);
    stream->active = 1;
    
    LOG_INFO("Started RTMP stream: %s", name);
    return 0;
}

void rtmp_stream_stop(RTMPStream* stream) {
    if (!stream) return;
    
    stream->active = 0;
    LOG_INFO("Stopped RTMP stream: %s", stream->name);
}