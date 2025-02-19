#include "rtmp_session.h"
#include "rtmp_log.h"
#include "rtmp_preview.h"
#include "rtmp_amf.h"
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define RTMP_STATE_INIT          0
#define RTMP_STATE_HANDSHAKE     1
#define RTMP_STATE_CONNECTED     2
#define RTMP_STATE_READY         3
#define RTMP_STATE_PUBLISHING    4
#define RTMP_STATE_PLAYING       5

struct RTMPSession {
    uint32_t id;
    int socket;
    uint8_t state;
    char app[128];
    char stream_key[128];
    uint32_t stream_id;
    uint32_t chunk_size;
    uint32_t window_ack_size;
    uint32_t bytes_received;
    void* user_data;
};

static uint32_t next_session_id = 1;

RTMPSession* rtmp_session_create(void) {
    RTMPSession* session = calloc(1, sizeof(RTMPSession));
    if (!session) {
        LOG_ERROR("Failed to allocate session");
        return NULL;
    }
    
    session->id = next_session_id++;
    session->state = RTMP_STATE_INIT;
    session->chunk_size = 128;
    session->window_ack_size = 2500000;
    
    LOG_INFO("Created new RTMP session %u", session->id);
    return session;
}

void rtmp_session_destroy(RTMPSession* session) {
    if (!session) return;
    
    LOG_INFO("Destroying RTMP session %u", session->id);
    free(session);
}

static int handle_connect(RTMPSession* session, AMFObject* obj) {
    if (!session || !obj) return -1;
    
    // Extrair app name do comando connect
    AMFObject* props = obj->next;
    if (props && props->value->type == AMF0_OBJECT) {
        AMFObject* app_prop = props->value->data.object;
        while (app_prop) {
            if (strcmp(app_prop->name, "app") == 0 && 
                app_prop->value->type == AMF0_STRING) {
                strncpy(session->app, app_prop->value->data.string, sizeof(session->app)-1);
                break;
            }
            app_prop = app_prop->next;
        }
    }
    
    LOG_INFO("RTMP session %u connect to app: %s", session->id, session->app);
    
    // Enviar respostas necessárias
    // _result para connect
    uint8_t response[1024];
    size_t offset = 0;
    
    // AMF0 String "_result"
    response[offset++] = AMF0_STRING;
    response[offset++] = 0;
    response[offset++] = 7;
    memcpy(response + offset, "_result", 7);
    offset += 7;
    
    // Transaction ID (mesmo do request)
    response[offset++] = AMF0_NUMBER;
    memcpy(response + offset, "\x00\x00\x00\x00\x00\x00\x00\x01", 8);
    offset += 8;
    
    // Properties object
    response[offset++] = AMF0_OBJECT;
    
    // fmsVer
    response[offset++] = 0;
    response[offset++] = 7;
    memcpy(response + offset, "fmsVer", 6);
    offset += 6;
    response[offset++] = AMF0_STRING;
    response[offset++] = 0;
    response[offset++] = 10;
    memcpy(response + offset, "FMS/3,0,1,123", 10);
    offset += 10;
    
    // capabilities
    response[offset++] = 0;
    response[offset++] = 12;
    memcpy(response + offset, "capabilities", 12);
    offset += 12;
    response[offset++] = AMF0_NUMBER;
    memcpy(response + offset, "\x00\x00\x00\x00\x00\x00\x00\x7F", 8);
    offset += 8;
    
    // Object end marker
    response[offset++] = 0;
    response[offset++] = 0;
    response[offset++] = 9;
    
    // Enviar resposta
    if (send(session->socket, response, offset, 0) < 0) {
        LOG_ERROR("Failed to send connect response");
        return -1;
    }
    
    session->state = RTMP_STATE_CONNECTED;
    return 0;
}

static int handle_createStream(RTMPSession* session, AMFObject* obj) {
    if (!session) return -1;
    
    session->stream_id = 1;
    LOG_INFO("RTMP session %u created stream: %u", session->id, session->stream_id);
    
    // Enviar _result
    uint8_t response[128];
    size_t offset = 0;
    
    // AMF0 String "_result"
    response[offset++] = AMF0_STRING;
    response[offset++] = 0;
    response[offset++] = 7;
    memcpy(response + offset, "_result", 7);
    offset += 7;
    
    // Transaction ID (mesmo do request)
    response[offset++] = AMF0_NUMBER;
    memcpy(response + offset, "\x00\x00\x00\x00\x00\x00\x00\x02", 8);
    offset += 8;
    
    // Null
    response[offset++] = AMF0_NULL;
    
    // Stream ID
    response[offset++] = AMF0_NUMBER;
    uint64_t stream_id = 1;
    memcpy(response + offset, &stream_id, 8);
    offset += 8;
    
    if (send(session->socket, response, offset, 0) < 0) {
        LOG_ERROR("Failed to send createStream response");
        return -1;
    }
    
    session->state = RTMP_STATE_READY;
    return 0;
}

static int handle_publish(RTMPSession* session, AMFObject* obj) {
    if (!session) return -1;
    
    // Extrair stream key
    AMFObject* stream_name = obj->next;
    if (stream_name && stream_name->value->type == AMF0_STRING) {
        strncpy(session->stream_key, stream_name->value->data.string, sizeof(session->stream_key)-1);
    }
    
    LOG_INFO("RTMP session %u start publish: %s/%s", 
             session->id, session->app, session->stream_key);
    
    // Enviar onStatus
    uint8_t response[256];
    size_t offset = 0;
    
    // AMF0 String "onStatus"
    response[offset++] = AMF0_STRING;
    response[offset++] = 0;
    response[offset++] = 8;
    memcpy(response + offset, "onStatus", 8);
    offset += 8;
    
    // Transaction ID = 0
    response[offset++] = AMF0_NUMBER;
    memcpy(response + offset, "\x00\x00\x00\x00\x00\x00\x00\x00", 8);
    offset += 8;
    
    // Null
    response[offset++] = AMF0_NULL;
    
    // Info object
    response[offset++] = AMF0_OBJECT;
    
    // level : status
    response[offset++] = 0;
    response[offset++] = 5;
    memcpy(response + offset, "level", 5);
    offset += 5;
    response[offset++] = AMF0_STRING;
    response[offset++] = 0;
    response[offset++] = 6;
    memcpy(response + offset, "status", 6);
    offset += 6;
    
    // code : NetStream.Publish.Start
    response[offset++] = 0;
    response[offset++] = 4;
    memcpy(response + offset, "code", 4);
    offset += 4;
    response[offset++] = AMF0_STRING;
    response[offset++] = 0;
    response[offset++] = 23;
    memcpy(response + offset, "NetStream.Publish.Start", 23);
    offset += 23;
    
    // Object end marker
    response[offset++] = 0;
    response[offset++] = 0;
    response[offset++] = 9;
    
    if (send(session->socket, response, offset, 0) < 0) {
        LOG_ERROR("Failed to send publish response");
        return -1;
    }
    
    session->state = RTMP_STATE_PUBLISHING;
    return 0;
}

static int handle_play(RTMPSession* session, AMFObject* obj) {
    if (!session) return -1;
    
    // Extrair stream key
    AMFObject* stream_name = obj->next;
    if (stream_name && stream_name->value->type == AMF0_STRING) {
        strncpy(session->stream_key, stream_name->value->data.string, sizeof(session->stream_key)-1);
    }
    
    LOG_INFO("RTMP session %u start play: %s/%s", 
             session->id, session->app, session->stream_key);
    
    // Enviar onStatus
    uint8_t response[256];
    size_t offset = 0;
    
    // AMF0 String "onStatus"
    response[offset++] = AMF0_STRING;
    response[offset++] = 0;
    response[offset++] = 8;
    memcpy(response + offset, "onStatus", 8);
    offset += 8;
    
    // Transaction ID = 0
    response[offset++] = AMF0_NUMBER;
    memcpy(response + offset, "\x00\x00\x00\x00\x00\x00\x00\x00", 8);
    offset += 8;
    
    // Null
    response[offset++] = AMF0_NULL;
    
    // Info object
    response[offset++] = AMF0_OBJECT;
    
    // level : status
    response[offset++] = 0;
    response[offset++] = 5;
    memcpy(response + offset, "level", 5);
    offset += 5;
    response[offset++] = AMF0_STRING;
    response[offset++] = 0;
    response[offset++] = 6;
    memcpy(response + offset, "status", 6);
    offset += 6;
    
    // code : NetStream.Play.Start
    response[offset++] = 0;
    response[offset++] = 4;
    memcpy(response + offset, "code", 4);
    offset += 4;
    response[offset++] = AMF0_STRING;
    response[offset++] = 0;
    response[offset++] = 20;
    memcpy(response + offset, "NetStream.Play.Start", 20);
    offset += 20;
    
    // Object end marker
    response[offset++] = 0;
    response[offset++] = 0;
    response[offset++] = 9;
    
    if (send(session->socket, response, offset, 0) < 0) {
        LOG_ERROR("Failed to send play response");
        return -1;
    }
    
    session->state = RTMP_STATE_PLAYING;
    return 0;
}

int rtmp_session_process_command(RTMPSession* session, const uint8_t* data, size_t len) {
    if (!session || !data || len == 0) return -1;
    
    size_t bytes_read;
    AMFValue* command_name = amf_decode(data, len, &bytes_read);
    if (!command_name || command_name->type != AMF0_STRING) {
        LOG_ERROR("Invalid command format");
        return -1;
    }
    
    data += bytes_read;
    len -= bytes_read;
    
    if (strcmp(command_name->data.string, "connect") == 0) {
        AMFObject* obj = amf_decode_object(data, len, &bytes_read);
        int result = handle_connect(session, obj);
        amf_object_free(obj);
        return result;
    }
    else if (strcmp(command_name->data.string, "createStream") == 0) {
        return handle_createStream(session, NULL);
    }
    else if (strcmp(command_name->data.string, "publish") == 0) {
        AMFObject* obj = amf_decode_object(data, len, &bytes_read);
        int result = handle_publish(session, obj);
        amf_object_free(obj);
        return result;
    }
    else if (strcmp(command_name->data.string, "play") == 0) {
        AMFObject* obj = amf_decode_object(data, len, &bytes_read);
        int result = handle_play(session, obj);
        amf_object_free(obj);
        return result;
    }
    
    LOG_WARNING("Unknown command: %s", command_name->data.string);
    return 0;
}

int rtmp_session_process_chunk(RTMPSession* session, RTMPChunk* chunk) {
    if (!session || !chunk) return -1;
    
    switch (chunk->type) {
        case RTMP_MSG_COMMAND_AMF0:
            return rtmp_session_process_command(session, chunk->data, chunk->length);
            
        case RTMP_MSG_VIDEO:
            if (session->state == RTMP_STATE_PUBLISHING) {
                rtmp_preview_process_video(chunk->data, chunk->length, chunk->timestamp);
            }
            break;
            
        case RTMP_MSG_AUDIO:
            if (session->state == RTMP_STATE_PUBLISHING) {
                rtmp_preview_process_audio(chunk->data, chunk->length, chunk->timestamp);
            }
            break;
    }
    
    return 0;
}