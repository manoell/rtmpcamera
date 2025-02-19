#include "rtmp_stream.h"
#include "rtmp_log.h"
#include "rtmp_preview.h"
#include <stdlib.h>
#include <string.h>

struct RTMPStream {
    char name[128];
    int active;
    
    rtmp_video_callback video_cb;
    rtmp_audio_callback audio_cb;
    void* video_ctx;
    void* audio_ctx;
    
    uint8_t* sps;
    size_t sps_size;
    uint8_t* pps;
    size_t pps_size;
    int has_video_config;
    
    uint8_t* aac_config;
    size_t aac_config_size;
    int has_audio_config;
};

RTMPStream* rtmp_stream_create(void) {
    RTMPStream* stream = calloc(1, sizeof(RTMPStream));
    if (!stream) {
        LOG_ERROR("Failed to allocate stream");
        return NULL;
    }
    
    LOG_INFO("Created new RTMP stream");
    return stream;
}

void rtmp_stream_destroy(RTMPStream* stream) {
    if (!stream) return;
    
    free(stream->sps);
    free(stream->pps);
    free(stream->aac_config);
    
    free(stream);
    LOG_INFO("Destroyed RTMP stream");
}

static int handle_video_config(RTMPStream* stream, const uint8_t* data, size_t len) {
    if (len < 10) return -1;
    
    // Skip AVC configuration version and profile/compatibility/level
    data += 5;
    len -= 5;
    
    // Skip NAL length size
    data++;
    len--;
    
    // Number of SPS
    uint8_t num_sps = data[0] & 0x1F;
    data++;
    len--;
    
    if (num_sps > 0) {
        if (len < 2) return -1;
        uint16_t sps_size = (data[0] << 8) | data[1];
        data += 2;
        len -= 2;
        
        if (len < sps_size) return -1;
        
        free(stream->sps);
        stream->sps = malloc(sps_size);
        if (!stream->sps) return -1;
        
        memcpy(stream->sps, data, sps_size);
        stream->sps_size = sps_size;
        
        data += sps_size;
        len -= sps_size;
    }
    
    if (len < 1) return -1;
    
    // Number of PPS
    uint8_t num_pps = data[0];
    data++;
    len--;
    
    if (num_pps > 0) {
        if (len < 2) return -1;
        uint16_t pps_size = (data[0] << 8) | data[1];
        data += 2;
        len -= 2;
        
        if (len < pps_size) return -1;
        
        free(stream->pps);
        stream->pps = malloc(pps_size);
        if (!stream->pps) return -1;
        
        memcpy(stream->pps, data, pps_size);
        stream->pps_size = pps_size;
    }
    
    stream->has_video_config = 1;
    LOG_INFO("Received video configuration: SPS size=%zu, PPS size=%zu",
             stream->sps_size, stream->pps_size);
    
    return 0;
}

static int handle_video_frame(RTMPStream* stream, const uint8_t* data, size_t len, uint32_t timestamp) {
    if (len < 5) return -1;
    
    // Skip frame type and codec ID
    data++;
    len--;
    
    // Skip composition time
    data += 3;
    len -= 3;
    
    while (len >= 4) {
        uint32_t nal_size = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
        data += 4;
        len -= 4;
        
        if (len < nal_size) break;
        
        if (stream->video_cb) {
            stream->video_cb(stream->video_ctx, data, nal_size, timestamp);
        }
        
        rtmp_preview_process_video(data, nal_size, timestamp);
        
        data += nal_size;
        len -= nal_size;
    }
    
    return 0;
}

int rtmp_stream_process_video(RTMPStream* stream, const uint8_t* data, size_t len, uint32_t timestamp) {
    if (!stream || !data || len < 2) return -1;
    
    uint8_t codec_id = data[0] & 0x0F;
    
    if (codec_id != 7) {
        LOG_WARNING("Unsupported video codec: %d", codec_id);
        return 0;
    }
    
    uint8_t avc_packet_type = data[1];
    
    switch (avc_packet_type) {
        case 0:
            return handle_video_config(stream, data + 2, len - 2);
            
        case 1:
            if (!stream->has_video_config) {
                LOG_WARNING("Received video frame before configuration");
                return 0;
            }
            return handle_video_frame(stream, data, len, timestamp);
            
        case 2:
            LOG_INFO("Received end of video sequence");
            return 0;
            
        default:
            LOG_WARNING("Unknown AVC packet type: %d", avc_packet_type);
            return -1;
    }
}

static int handle_audio_config(RTMPStream* stream, const uint8_t* data, size_t len) {
    if (len < 2) return -1;
    
    free(stream->aac_config);
    stream->aac_config = malloc(len);
    if (!stream->aac_config) return -1;
    
    memcpy(stream->aac_config, data, len);
    stream->aac_config_size = len;
    stream->has_audio_config = 1;
    
    LOG_INFO("Received audio configuration: %zu bytes", len);
    return 0;
}

int rtmp_stream_process_audio(RTMPStream* stream, const uint8_t* data, size_t len, uint32_t timestamp) {
    if (!stream || !data || len < 2) return -1;
    
    uint8_t format = (data[0] & 0xF0) >> 4;
    
    if (format != 10) {
        LOG_WARNING("Unsupported audio format: %d", format);
        return 0;
    }
    
    uint8_t aac_packet_type = data[1];
    data += 2;
    len -= 2;
    
    switch (aac_packet_type) {
        case 0:
            return handle_audio_config(stream, data, len);
            
        case 1:
            if (!stream->has_audio_config) {
                LOG_WARNING("Received audio frame before configuration");
                return 0;
            }
            
            if (stream->audio_cb) {
                stream->audio_cb(stream->audio_ctx, data, len, timestamp);
            }
            
            rtmp_preview_process_audio(data, len, timestamp);
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