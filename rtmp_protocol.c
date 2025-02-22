#include "rtmp_protocol.h"
#include "rtmp_chunk.h"
#include "rtmp_diagnostics.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// RTMP message types
#define RTMP_MSG_SetChunkSize     1
#define RTMP_MSG_Abort            2
#define RTMP_MSG_Ack              3
#define RTMP_MSG_UserControl      4
#define RTMP_MSG_WindowAckSize    5
#define RTMP_MSG_SetPeerBandwidth 6
#define RTMP_MSG_AudioMessage     8
#define RTMP_MSG_VideoMessage     9
#define RTMP_MSG_Command         20
#define RTMP_MSG_DataMessage     18
#define RTMP_MSG_SharedObject    19

// RTMP user control message types
#define RTMP_USER_STREAM_BEGIN     0
#define RTMP_USER_STREAM_EOF       1
#define RTMP_USER_STREAM_DRY       2
#define RTMP_USER_SET_BUFFER_LEN   3
#define RTMP_USER_STREAM_IS_REC    4
#define RTMP_USER_PING_REQUEST     6
#define RTMP_USER_PING_RESPONSE    7

// Protocol context
static struct {
    uint32_t chunk_size;
    uint32_t window_size;
    uint32_t bandwidth;
    uint32_t stream_id;
    bool initialized;
} protocol_context = {
    .chunk_size = RTMP_DEFAULT_CHUNK_SIZE,
    .window_size = 2500000,
    .bandwidth = 2500000,
    .stream_id = 1,
    .initialized = false
};

void rtmp_protocol_init(void) {
    if (protocol_context.initialized) return;
    protocol_context.initialized = true;
}

RTMPPacket* rtmp_protocol_create_connect(const char *app, const char *tcUrl) {
    RTMPPacket *packet = (RTMPPacket*)calloc(1, sizeof(RTMPPacket));
    if (!packet) return NULL;
    
    // Create connect command
    char connect_cmd[1024];
    snprintf(connect_cmd, sizeof(connect_cmd),
             "{\"app\":\"%s\",\"flashVer\":\"FMLE/3.0\",\"tcUrl\":\"%s\","
             "\"type\":\"nonprivate\",\"capabilities\":255}", 
             app, tcUrl);
    
    size_t cmd_len = strlen(connect_cmd);
    packet->m_body = (uint8_t*)malloc(cmd_len + 1);
    if (!packet->m_body) {
        free(packet);
        return NULL;
    }
    
    memcpy(packet->m_body, connect_cmd, cmd_len + 1);
    
    packet->m_headerType = 0;
    packet->m_packetType = RTMP_MSG_Command;
    packet->m_nChannel = 3;
    packet->m_nTimeStamp = 0;
    packet->m_nBodySize = cmd_len;
    packet->m_nInfoField2 = 0;
    
    return packet;
}

RTMPPacket* rtmp_protocol_create_play(const char *stream_name) {
    RTMPPacket *packet = (RTMPPacket*)calloc(1, sizeof(RTMPPacket));
    if (!packet) return NULL;
    
    // Create play command
    char play_cmd[512];
    snprintf(play_cmd, sizeof(play_cmd),
             "{\"command\":\"play\",\"streamName\":\"%s\"}", 
             stream_name);
    
    size_t cmd_len = strlen(play_cmd);
    packet->m_body = (uint8_t*)malloc(cmd_len + 1);
    if (!packet->m_body) {
        free(packet);
        return NULL;
    }
    
    memcpy(packet->m_body, play_cmd, cmd_len + 1);
    
    packet->m_headerType = 0;
    packet->m_packetType = RTMP_MSG_Command;
    packet->m_nChannel = 8;
    packet->m_nTimeStamp = 0;
    packet->m_nBodySize = cmd_len;
    packet->m_nInfoField2 = protocol_context.stream_id;
    
    return packet;
}

RTMPPacket* rtmp_protocol_create_publish(const char *stream_name) {
    RTMPPacket *packet = (RTMPPacket*)calloc(1, sizeof(RTMPPacket));
    if (!packet) return NULL;
    
    // Create publish command
    char publish_cmd[512];
    snprintf(publish_cmd, sizeof(publish_cmd),
             "{\"command\":\"publish\",\"streamName\":\"%s\",\"type\":\"live\"}", 
             stream_name);
    
    size_t cmd_len = strlen(publish_cmd);
    packet->m_body = (uint8_t*)malloc(cmd_len + 1);
    if (!packet->m_body) {
        free(packet);
        return NULL;
    }
    
    memcpy(packet->m_body, publish_cmd, cmd_len + 1);
    
    packet->m_headerType = 0;
    packet->m_packetType = RTMP_MSG_Command;
    packet->m_nChannel = 8;
    packet->m_nTimeStamp = 0;
    packet->m_nBodySize = cmd_len;
    packet->m_nInfoField2 = protocol_context.stream_id;
    
    return packet;
}

RTMPPacket* rtmp_protocol_create_set_chunk_size(uint32_t chunk_size) {
    RTMPPacket *packet = (RTMPPacket*)calloc(1, sizeof(RTMPPacket));
    if (!packet) return NULL;
    
    packet->m_body = (uint8_t*)malloc(4);
    if (!packet->m_body) {
        free(packet);
        return NULL;
    }
    
    // Network byte order
    packet->m_body[0] = (chunk_size >> 24) & 0xFF;
    packet->m_body[1] = (chunk_size >> 16) & 0xFF;
    packet->m_body[2] = (chunk_size >> 8) & 0xFF;
    packet->m_body[3] = chunk_size & 0xFF;
    
    packet->m_headerType = 0;
    packet->m_packetType = RTMP_MSG_SetChunkSize;
    packet->m_nChannel = 2;
    packet->m_nTimeStamp = 0;
    packet->m_nBodySize = 4;
    packet->m_nInfoField2 = 0;
    
    return packet;
}

RTMPPacket* rtmp_protocol_create_window_ack_size(uint32_t window_size) {
    RTMPPacket *packet = (RTMPPacket*)calloc(1, sizeof(RTMPPacket));
    if (!packet) return NULL;
    
    packet->m_body = (uint8_t*)malloc(4);
    if (!packet->m_body) {
        free(packet);
        return NULL;
    }
    
    packet->m_body[0] = (window_size >> 24) & 0xFF;
    packet->m_body[1] = (window_size >> 16) & 0xFF;
    packet->m_body[2] = (window_size >> 8) & 0xFF;
    packet->m_body[3] = window_size & 0xFF;
    
    packet->m_headerType = 0;
    packet->m_packetType = RTMP_MSG_WindowAckSize;
    packet->m_nChannel = 2;
    packet->m_nTimeStamp = 0;
    packet->m_nBodySize = 4;
    packet->m_nInfoField2 = 0;
    
    return packet;
}

RTMPPacket* rtmp_protocol_create_ping(void) {
    RTMPPacket *packet = (RTMPPacket*)calloc(1, sizeof(RTMPPacket));
    if (!packet) return NULL;
    
    packet->m_body = (uint8_t*)malloc(6);
    if (!packet->m_body) {
        free(packet);
        return NULL;
    }
    
    // Ping type (request)
    packet->m_body[0] = 0;
    packet->m_body[1] = RTMP_USER_PING_REQUEST;
    
    // Timestamp
    uint32_t timestamp = (uint32_t)time(NULL);
    packet->m_body[2] = (timestamp >> 24) & 0xFF;
    packet->m_body[3] = (timestamp >> 16) & 0xFF;
    packet->m_body[4] = (timestamp >> 8) & 0xFF;
    packet->m_body[5] = timestamp & 0xFF;
    
    packet->m_headerType = 0;
    packet->m_packetType = RTMP_MSG_UserControl;
    packet->m_nChannel = 2;
    packet->m_nTimeStamp = 0;
    packet->m_nBodySize = 6;
    packet->m_nInfoField2 = 0;
    
    return packet;
}

bool rtmp_protocol_parse_url(const char *url, char *hostname, size_t hostname_size,
                           int *port, char *app_name, size_t app_size,
                           char *stream_name, size_t stream_size) {
    if (!url || !hostname || !port || !app_name || !stream_name) return false;
    
    // Check for rtmp:// prefix
    if (strncmp(url, "rtmp://", 7) != 0) return false;
    
    const char *p = url + 7;
    const char *host_end = strchr(p, '/');
    if (!host_end) return false;
    
    // Extract hostname and port
    size_t host_len = host_end - p;
    const char *port_start = strchr(p, ':');
    
    if (port_start && port_start < host_end) {
        host_len = port_start - p;
        *port = atoi(port_start + 1);
    } else {
        *port = RTMP_DEFAULT_PORT;
    }
    
    if (host_len >= hostname_size) return false;
    strncpy(hostname, p, host_len);
    hostname[host_len] = '\0';
    
    // Extract app name and stream name
    p = host_end + 1;
    const char *app_end = strchr(p, '/');
    if (!app_end) {
        if (strlen(p) >= app_size) return false;
        strcpy(app_name, p);
        stream_name[0] = '\0';
    } else {
        size_t app_len = app_end - p;
        if (app_len >= app_size) return false;
        strncpy(app_name, p, app_len);
        app_name[app_len] = '\0';
        
        p = app_end + 1;
        if (strlen(p) >= stream_size) return false;
        strcpy(stream_name, p);
    }
    
    return true;
}

uint32_t rtmp_protocol_get_chunk_size(const RTMPPacket *packet) {
    if (!packet || packet->m_nBodySize < 4) return RTMP_DEFAULT_CHUNK_SIZE;
    
    return (packet->m_body[0] << 24) | (packet->m_body[1] << 16) |
           (packet->m_body[2] << 8) | packet->m_body[3];
}

uint32_t rtmp_protocol_get_window_size(const RTMPPacket *packet) {
    if (!packet || packet->m_nBodySize < 4) return 0;
    
    return (packet->m_body[0] << 24) | (packet->m_body[1] << 16) |
           (packet->m_body[2] << 8) | packet->m_body[3];
}

void rtmp_protocol_handle_packet(const RTMPPacket *packet) {
    if (!packet) return;
    
    switch (packet->m_packetType) {
        case RTMP_MSG_SetChunkSize:
            protocol_context.chunk_size = rtmp_protocol_get_chunk_size(packet);
            rtmp_chunk_set_size(protocol_context.chunk_size);
            break;
            
        case RTMP_MSG_WindowAckSize:
            protocol_context.window_size = rtmp_protocol_get_window_size(packet);
            break;
            
        case RTMP_MSG_SetPeerBandwidth:
            if (packet->m_nBodySize >= 4) {
                protocol_context.bandwidth = (packet->m_body[0] << 24) |
                                          (packet->m_body[1] << 16) |
                                          (packet->m_body[2] << 8) |
                                           packet->m_body[3];
            }
            break;
            
        default:
            break;
    }
}

void rtmp_protocol_set_stream_id(uint32_t stream_id) {
    protocol_context.stream_id = stream_id;
}