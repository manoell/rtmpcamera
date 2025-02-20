// rtmp_stream.h
#ifndef RTMP_STREAM_H
#define RTMP_STREAM_H

#include "rtmp_core.h"
#include "rtmp_session.h"
#include <stdint.h>

// Tipos de pacotes de vídeo
#define RTMP_VIDEO_KEYFRAME          0x01
#define RTMP_VIDEO_INTER_FRAME       0x02
#define RTMP_VIDEO_DISPOSABLE_FRAME  0x03
#define RTMP_VIDEO_GENERATED_FRAME   0x04
#define RTMP_VIDEO_INFO_FRAME        0x05

// Codecs de vídeo
#define RTMP_VIDEO_CODEC_JPEG    0x01
#define RTMP_VIDEO_CODEC_H263    0x02
#define RTMP_VIDEO_CODEC_SCREEN  0x03
#define RTMP_VIDEO_CODEC_VP6     0x04
#define RTMP_VIDEO_CODEC_VP6A    0x05
#define RTMP_VIDEO_CODEC_SCREEN2 0x06
#define RTMP_VIDEO_CODEC_H264    0x07
#define RTMP_VIDEO_CODEC_H265    0x08

// Estrutura do stream
typedef struct {
    uint8_t type;
    uint8_t codec;
    uint32_t timestamp;
    uint8_t* data;
    uint32_t length;
    bool is_keyframe;
    bool is_sequence_header;
} RTMPVideoPacket;

typedef struct {
    uint8_t codec;
    uint32_t timestamp;
    uint8_t* data;
    uint32_t length;
    bool is_sequence_header;
} RTMPAudioPacket;

// Funções de processamento de vídeo
int rtmp_stream_process_video(RTMPSession* session, RTMPMessage* message);
int rtmp_stream_parse_video_packet(uint8_t* data, uint32_t length, RTMPVideoPacket* packet);
void rtmp_stream_free_video_packet(RTMPVideoPacket* packet);

// Funções de processamento de áudio
int rtmp_stream_process_audio(RTMPSession* session, RTMPMessage* message);
int rtmp_stream_parse_audio_packet(uint8_t* data, uint32_t length, RTMPAudioPacket* packet);
void rtmp_stream_free_audio_packet(RTMPAudioPacket* packet);

// Funções de análise de qualidade
void rtmp_stream_analyze_quality(RTMPSession* session, RTMPVideoPacket* packet);
const char* rtmp_stream_get_codec_name(uint8_t codec);

#endif