// rtmp_stream.c
#include "rtmp_stream.h"
#include <stdlib.h>
#include <string.h>

// Estrutura para análise de qualidade
typedef struct {
    uint32_t last_keyframe_timestamp;
    uint32_t frames_since_keyframe;
    uint32_t total_bytes;
    uint32_t frame_count;
    float avg_frame_size;
    uint32_t dropped_frames;
} StreamQualityMetrics;

static StreamQualityMetrics quality_metrics = {0};

int rtmp_stream_process_video(RTMPSession* session, RTMPMessage* message) {
    if (!session || !message || !message->payload) {
        return RTMP_ERROR_MEMORY;
    }

    RTMPVideoPacket packet = {0};
    int ret = rtmp_stream_parse_video_packet(message->payload, message->message_length, &packet);
    if (ret != RTMP_OK) {
        return ret;
    }

    // Atualiza informações do stream
    session->stream_info.video_codec[0] = '\0';
    strncpy(session->stream_info.video_codec, 
            rtmp_stream_get_codec_name(packet.codec),
            sizeof(session->stream_info.video_codec) - 1);

    // Analisa qualidade do stream
    rtmp_stream_analyze_quality(session, &packet);

    // Se for um keyframe com sequence header, extrai resolução
    if (packet.is_keyframe && packet.is_sequence_header) {
        if (packet.codec == RTMP_VIDEO_CODEC_H264) {
            // Parse do SPS para obter resolução
            // Nota: Implementação simplificada, ajustar conforme necessário
            uint8_t* sps = packet.data;
            uint32_t sps_size = packet.length;
            
            // Exemplo de log para análise do SPS
            rtmp_log(RTMP_LOG_DEBUG, "Received H264 SPS (size: %u)", sps_size);
            if (sps_size > 0) {
                rtmp_log(RTMP_LOG_DEBUG, "SPS first byte: 0x%02X", sps[0]);
            }
        }
    }

    rtmp_stream_free_video_packet(&packet);
    return RTMP_OK;
}

int rtmp_stream_parse_video_packet(uint8_t* data, uint32_t length, RTMPVideoPacket* packet) {
    if (!data || !packet || length < 2) {
        return RTMP_ERROR_PROTOCOL;
    }

    packet->type = (data[0] >> 4) & 0x0F;
    packet->codec = data[0] & 0x0F;
    packet->is_keyframe = (packet->type == RTMP_VIDEO_KEYFRAME);
    
    // O segundo byte contém flags adicionais
    packet->is_sequence_header = (data[1] == 0);

    // Copia os dados do payload
    packet->length = length - 2;
    packet->data = malloc(packet->length);
    if (!packet->data) {
        return RTMP_ERROR_MEMORY;
    }
    memcpy(packet->data, data + 2, packet->length);

    rtmp_log(RTMP_LOG_DEBUG, "Video packet: codec=%s, keyframe=%d, seq_header=%d, size=%u",
             rtmp_stream_get_codec_name(packet->codec),
             packet->is_keyframe,
             packet->is_sequence_header,
             packet->length);

    return RTMP_OK;
}

void rtmp_stream_free_video_packet(RTMPVideoPacket* packet) {
    if (packet) {
        free(packet->data);
        packet->data = NULL;
    }
}

int rtmp_stream_process_audio(RTMPSession* session, RTMPMessage* message) {
    if (!session || !message || !message->payload) {
        return RTMP_ERROR_MEMORY;
    }

    RTMPAudioPacket packet = {0};
    int ret = rtmp_stream_parse_audio_packet(message->payload, message->message_length, &packet);
    if (ret != RTMP_OK) {
        return ret;
    }

    // Atualiza bitrate de áudio
    session->stream_info.audio_bitrate = (packet.length * 8 * 1000) / 
                                       (message->timestamp - session->stream_info.start_time);

    rtmp_stream_free_audio_packet(&packet);
    return RTMP_OK;
}

int rtmp_stream_parse_audio_packet(uint8_t* data, uint32_t length, RTMPAudioPacket* packet) {
    if (!data || !packet || length < 1) {
        return RTMP_ERROR_PROTOCOL;
    }

    packet->codec = (data[0] >> 4) & 0x0F;
    packet->is_sequence_header = (data[1] == 0);

    packet->length = length - 2;
    packet->data = malloc(packet->length);
    if (!packet->data) {
        return RTMP_ERROR_MEMORY;
    }
    memcpy(packet->data, data + 2, packet->length);

    return RTMP_OK;
}

void rtmp_stream_free_audio_packet(RTMPAudioPacket* packet) {
    if (packet) {
        free(packet->data);
        packet->data = NULL;
    }
}

void rtmp_stream_analyze_quality(RTMPSession* session, RTMPVideoPacket* packet) {
    if (!session || !packet) return;

    quality_metrics.frame_count++;
    quality_metrics.total_bytes += packet->length;
    quality_metrics.avg_frame_size = (float)quality_metrics.total_bytes / quality_metrics.frame_count;

    if (packet->is_keyframe) {
        uint32_t keyframe_interval = 0;
        if (quality_metrics.last_keyframe_timestamp > 0) {
            keyframe_interval = packet->timestamp - quality_metrics.last_keyframe_timestamp;
        }
        quality_metrics.last_keyframe_timestamp = packet->timestamp;
        quality_metrics.frames_since_keyframe = 0;

        rtmp_log(RTMP_LOG_INFO, "Keyframe received - Interval: %u ms", keyframe_interval);
    } else {
        quality_metrics.frames_since_keyframe++;
    }

    // Calcula bitrate de vídeo
    if (packet->timestamp > session->stream_info.start_time) {
        session->stream_info.video_bitrate = 
            (quality_metrics.total_bytes * 8 * 1000) / 
            (packet->timestamp - session->stream_info.start_time);
    }

    // Log periódico de qualidade (a cada 100 frames)
    if (quality_metrics.frame_count % 100 == 0) {
        rtmp_log(RTMP_LOG_INFO, "Stream Quality Metrics:");
        rtmp_log(RTMP_LOG_INFO, "  Average Frame Size: %.2f bytes", quality_metrics.avg_frame_size);
        rtmp_log(RTMP_LOG_INFO, "  Video Bitrate: %u kbps", session->stream_info.video_bitrate / 1000);
        rtmp_log(RTMP_LOG_INFO, "  Frames Since Last Keyframe: %u", quality_metrics.frames_since_keyframe);
        rtmp_log(RTMP_LOG_INFO, "  Dropped Frames: %u", quality_metrics.dropped_frames);
    }
}

const char* rtmp_stream_get_codec_name(uint8_t codec) {
    switch (codec) {
        case RTMP_VIDEO_CODEC_H264: return "H264";
        case RTMP_VIDEO_CODEC_H265: return "H265";
        case RTMP_VIDEO_CODEC_VP6:  return "VP6";
        default: return "Unknown";
    }
}