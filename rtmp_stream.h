#ifndef RTMP_STREAM_H
#define RTMP_STREAM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RTMPStream RTMPStream;

typedef void (*rtmp_video_callback)(void* ctx, const uint8_t* data, size_t len, uint32_t timestamp);
typedef void (*rtmp_audio_callback)(void* ctx, const uint8_t* data, size_t len, uint32_t timestamp);

RTMPStream* rtmp_stream_create(void);
void rtmp_stream_destroy(RTMPStream* stream);

int rtmp_stream_process_video(RTMPStream* stream, const uint8_t* data, size_t len, uint32_t timestamp);
int rtmp_stream_process_audio(RTMPStream* stream, const uint8_t* data, size_t len, uint32_t timestamp);

void rtmp_stream_set_video_callback(RTMPStream* stream, rtmp_video_callback cb, void* ctx);
void rtmp_stream_set_audio_callback(RTMPStream* stream, rtmp_audio_callback cb, void* ctx);

int rtmp_stream_start(RTMPStream* stream, const char* name);
void rtmp_stream_stop(RTMPStream* stream);

#ifdef __cplusplus
}
#endif

#endif // RTMP_STREAM_H