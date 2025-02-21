#ifndef RTMP_COMMANDS_H
#define RTMP_COMMANDS_H

#include "rtmp_core.h"
#include "rtmp_utils.h"

// RTMP Chunk Format
#define RTMP_CHUNK_TYPE_0 0  // 11-byte header
#define RTMP_CHUNK_TYPE_1 1  // 7-byte header
#define RTMP_CHUNK_TYPE_2 2  // 3-byte header
#define RTMP_CHUNK_TYPE_3 3  // 0-byte header

// RTMP Message Types
#define RTMP_MSG_SET_CHUNK_SIZE     1
#define RTMP_MSG_ABORT              2
#define RTMP_MSG_ACKNOWLEDGEMENT    3
#define RTMP_MSG_USER_CONTROL       4
#define RTMP_MSG_WINDOW_ACK_SIZE    5
#define RTMP_MSG_SET_PEER_BANDWIDTH 6
#define RTMP_MSG_AUDIO             8
#define RTMP_MSG_VIDEO             9
#define RTMP_MSG_DATA_AMF3         15
#define RTMP_MSG_SHARED_OBJ_AMF3   16
#define RTMP_MSG_COMMAND_AMF3      17
#define RTMP_MSG_DATA_AMF0         18
#define RTMP_MSG_SHARED_OBJ_AMF0   19
#define RTMP_MSG_COMMAND_AMF0      20

// RTMP Chunk Header
typedef struct {
    uint8_t fmt;           // Format type
    uint32_t csid;         // Chunk stream ID
    uint32_t timestamp;    // Timestamp
    uint32_t length;       // Message length
    uint8_t type_id;       // Message type ID
    uint32_t stream_id;    // Message stream ID
} RTMPChunkHeader;

int rtmp_read_chunk_header(RTMPClient *client, RTMPChunkHeader *header);
int rtmp_handle_packet(RTMPClient *client, uint8_t *data, size_t size);

#endif