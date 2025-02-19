#ifndef RTMP_CORE_H
#define RTMP_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// RTMP Chunk Types
#define RTMP_CHUNK_TYPE_0       0  // 11 bytes
#define RTMP_CHUNK_TYPE_1       1  // 7 bytes
#define RTMP_CHUNK_TYPE_2       2  // 3 bytes
#define RTMP_CHUNK_TYPE_3       3  // 0 bytes

// RTMP Message Types
#define RTMP_MSG_CHUNK_SIZE     1
#define RTMP_MSG_ABORT          2
#define RTMP_MSG_ACK            3
#define RTMP_MSG_USER_CONTROL   4
#define RTMP_MSG_WINDOW_ACK     5
#define RTMP_MSG_SET_PEER_BW    6
#define RTMP_MSG_AUDIO          8
#define RTMP_MSG_VIDEO          9
#define RTMP_MSG_DATA_AMF3      15
#define RTMP_MSG_SHARED_OBJ_AMF3 16
#define RTMP_MSG_COMMAND_AMF3   17
#define RTMP_MSG_DATA_AMF0      18
#define RTMP_MSG_SHARED_OBJ_AMF0 19
#define RTMP_MSG_COMMAND_AMF0   20
#define RTMP_MSG_AGGREGATE      22

typedef struct RTMPContext RTMPContext;
typedef struct RTMPChunk RTMPChunk;
typedef struct RTMPSession RTMPSession;

typedef int (*RTMPVideoCallback)(void* userdata, const uint8_t* data, size_t len, uint32_t timestamp);
typedef int (*RTMPAudioCallback)(void* userdata, const uint8_t* data, size_t len, uint32_t timestamp);

RTMPContext* rtmp_context_create(void);
void rtmp_context_destroy(RTMPContext* ctx);
void rtmp_set_video_callback(RTMPContext* ctx, RTMPVideoCallback cb, void* userdata);
void rtmp_set_audio_callback(RTMPContext* ctx, RTMPAudioCallback cb, void* userdata);
int rtmp_process_packet(RTMPContext* ctx, const uint8_t* data, size_t len);
int rtmp_send_packet(RTMPContext* ctx, uint8_t type, const uint8_t* data, size_t len);
int rtmp_server_start(int port);
void rtmp_server_stop(void);
RTMPSession* rtmp_session_create(RTMPContext* ctx);
void rtmp_session_destroy(RTMPSession* session);
int rtmp_session_process(RTMPSession* session, const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif

#endif // RTMP_CORE_H