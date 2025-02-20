#include "rtmp_session.h"
#include "rtmp_log.h"
#include "rtmp_preview.h"
#include "rtmp_amf.h"
#include "rtmp_chunk.h"
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

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

    RTMPChunkStream* chunk_stream;
    RTMPStream* stream;
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
    session->chunk_size = RTMP_DEFAULT_CHUNK_SIZE;
    session->window_ack_size = 2500000;
    
    session->chunk_stream = rtmp_chunk_stream_create();
    if (!session->chunk_stream) {
        LOG_ERROR("Failed to create chunk stream");
        free(session);
        return NULL;
    }
    
    LOG_INFO("Created new RTMP session %u", session->id);
    return session;
}

void rtmp_session_destroy(RTMPSession* session) {
    if (!session) return;
    
    LOG_INFO("Destroying RTMP session %u", session->id);
    
    if (session->stream) {
        rtmp_stream_destroy(session->stream);
    }
    
    if (session->chunk_stream) {
        rtmp_chunk_stream_destroy(session->chunk_stream);
    }
    
    if (session->socket >= 0) {
        close(session->socket);
    }
    
    free(session);
}

static int send_chunk(RTMPSession* session, uint8_t type, const uint8_t* data, size_t len) {
    if (!session || !data || !len) return -1;
    
    uint8_t buffer[4096];
    size_t bytes_written;
    
    RTMPChunk chunk = {0};
    chunk.type = type;
    chunk.data = (uint8_t*)data;
    chunk.length = len;
    
    if (rtmp_chunk_write(session->chunk_stream, &chunk, buffer, sizeof(buffer), &bytes_written) < 0) {
        LOG_ERROR("Failed to write chunk");
        return -1;
    }
    
    if (send(session->socket, buffer, bytes_written, 0) < 0) {
        LOG_ERROR("Failed to send chunk");
        return -1;
    }
    
    return 0;
}

static int handle_connect(RTMPSession* session, AMFObject* obj) {
    if (!session || !obj) return -1;
    
    // Extrair app name
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
    
    LOG_INFO("Session %u connect: app=%s", session->id, session->app);
    
    // Enviar Window Acknowledgement Size
    uint8_t ack_size[4];
    write_uint32(ack_size, session->window_ack_size);
    if (send_chunk(session, RTMP_MSG_WINDOW_ACK, ack_size, 4) < 0) {
        return -1;
    }
    
    // Enviar Set Peer Bandwidth
    uint8_t bw_size[5];
    write_uint32(bw_size, session->window_ack_size);
    bw_size[4] = 2; // Dynamic
    if (send_chunk(session, RTMP_MSG_SET_PEER_BW, bw_size, 5) < 0) {
        return -1;
    }
    
    // Enviar Stream Begin
    uint8_t stream_begin[6];
    write_uint16(stream_begin, RTMP_EVENT_STREAM_BEGIN);
    write_uint32(stream_begin + 2, 0);
    if (send_chunk(session, RTMP_MSG_USER_CONTROL, stream_begin, 6) < 0) {
        return -1;
    }
    
    // Enviar resultado do connect
    uint8_t response[384];
    size_t response_len;
    if (amf_encode_connect_response(response, sizeof(response), &response_len) < 0) {
        return -1;
    }
    
    if (send_chunk(session, RTMP_MSG_COMMAND_AMF0, response, response_len) < 0) {
        return -1;
    }
    
    session->state = RTMP_STATE_CONNECTED;
    return 0;
}

static int handle_createStream(RTMPSession* session, AMFObject* obj) {
    if (!session) return -1;
    
    session->stream_id = 1;
    LOG_INFO("Session %u created stream: %u", session->id, session->stream_id);
    
    // Enviar resposta
    uint8_t response[256];
    size_t response_len;
    if (amf_encode_create_stream_response(response, sizeof(response), 
                                        3.0, session->stream_id, 
                                        &response_len) < 0) {
        return -1;
    }
    
    if (send_chunk(session, RTMP_MSG_COMMAND_AMF0, response, response_len) < 0) {
        return -1;
    }
    
    session->state = RTMP_STATE_READY;
    return 0;
}

static int handle_publish(RTMPSession* session, AMFObject* obj) {
    if (!session) return -1;
    
    // Extrair stream name
    AMFObject* stream_name = obj->next;
    if (stream_name && stream_name->value->type == AMF0_STRING) {
        strncpy(session->stream_key, stream_name->value->data.string,
                sizeof(session->stream_key)-1);
    }
    
    LOG_INFO("Session %u publish: %s/%s", session->id, session->app, session->stream_key);
    
    // Criar stream se necessário
    if (!session->stream) {
        session->stream = rtmp_stream_create();
        if (!session->stream) {
            return -1;
        }
    }
    
    // Iniciar stream
    rtmp_stream_start(session->stream, session->stream_key);
    
    // Mostrar preview
    rtmp_preview_show();
    
    // Enviar resposta
    uint8_t response[384];
    size_t response_len;
    if (amf_encode_publish_response(response, sizeof(response), 
                                  session->stream_key,
                                  &response_len) < 0) {
        return -1;
    }
    
    if (send_chunk(session, RTMP_MSG_COMMAND_AMF0, response, response_len) < 0) {
        return -1;
    }
    
    session->state = RTMP_STATE_PUBLISHING;
    return 0;
}

static int handle_play(RTMPSession* session, AMFObject* obj) {
    if (!session) return -1;
    
    // Extrair stream name
    AMFObject* stream_name = obj->next;
    if (stream_name && stream_name->value->type == AMF0_STRING) {
        strncpy(session->stream_key, stream_name->value->data.string,
                sizeof(session->stream_key)-1);
    }
    
    LOG_INFO("Session %u play: %s/%s", session->id, session->app, session->stream_key);
    
    // Enviar User Control - Stream Begin
    uint8_t stream_begin[6];
    write_uint16(stream_begin, RTMP_EVENT_STREAM_BEGIN);
    write_uint32(stream_begin + 2, session->stream_id);
    if (send_chunk(session, RTMP_MSG_USER_CONTROL, stream_begin, 6) < 0) {
        return -1;
    }
    
    // Enviar resposta
    uint8_t response[384];
    size_t response_len;
    if (amf_encode_play_response(response, sizeof(response), 
                                session->stream_key,
                                &response_len) < 0) {
        return -1;
    }
    
    if (send_chunk(session, RTMP_MSG_COMMAND_AMF0, response, response_len) < 0) {
        return -1;
    }
    
    session->state = RTMP_STATE_PLAYING;
    return 0;
}

int rtmp_session_process_chunk(RTMPSession* session, RTMPChunk* chunk) {
    if (!session || !chunk) return -1;
    
    switch (chunk->type) {
        case RTMP_MSG_CHUNK_SIZE:
            if (chunk->length >= 4) {
                session->chunk_size = read_uint32(chunk->data);
                rtmp_chunk_update_size(session->chunk_stream, session->chunk_size);
                LOG_DEBUG("Session %u: chunk size updated to %u", 
                         session->id, session->chunk_size);
            }
            break;
            
        case RTMP_MSG_WINDOW_ACK:
            if (chunk->length >= 4) {
                session->window_ack_size = read_uint32(chunk->data);
                LOG_DEBUG("Session %u: window ack size updated to %u",
                         session->id, session->window_ack_size);
            }
            break;
            
        case RTMP_MSG_COMMAND_AMF0:
            {
                size_t bytes_read;
                AMFValue* command = amf_decode(chunk->data, chunk->length, &bytes_read);
                if (!command || command->type != AMF0_STRING) {
                    if (command) amf_value_free(command);
                    return -1;
                }
                
                const char* cmd_name = command->data.string;
                AMFObject* obj = amf_decode_object(chunk->data + bytes_read,
                                                 chunk->length - bytes_read,
                                                 &bytes_read);
                
                if (strcmp(cmd_name, RTMP_CMD_CONNECT) == 0) {
                    handle_connect(session, obj);
                }
                else if (strcmp(cmd_name, RTMP_CMD_CREATE_STREAM) == 0) {
                    handle_createStream(session, obj);
                }
                else if (strcmp(cmd_name, RTMP_CMD_PUBLISH) == 0) {
                    handle_publish(session, obj);
                }
                else if (strcmp(cmd_name, RTMP_CMD_PLAY) == 0) {
                    handle_play(session, obj);
                }
                else {
                    LOG_DEBUG("Session %u: unhandled command: %s",
                             session->id, cmd_name);
                }
                
                amf_value_free(command);
                if (obj) amf_object_free(obj);
            }
            break;
            
        case RTMP_MSG_VIDEO:
            if (session->stream) {
                rtmp_stream_process_video(session->stream, chunk->data,
                                        chunk->length, chunk->timestamp);
            }
            break;
            
        case RTMP_MSG_AUDIO:
            if (session->stream) {
                rtmp_stream_process_audio(session->stream, chunk->data,
                                        chunk->length, chunk->timestamp);
            }
            break;
            
        default:
            LOG_DEBUG("Session %u: unhandled message type: %d",
                     session->id, chunk->type);
            break;
    }
    
    return 0;
}

int rtmp_session_send_chunk(RTMPSession* session, RTMPChunk* chunk) {
    if (!session || !chunk) return -1;
    
    uint8_t buffer[4096];
    size_t bytes_written;
    
    if (rtmp_chunk_write(session->chunk_stream, chunk, buffer,
                        sizeof(buffer), &bytes_written) < 0) {
        return -1;
    }
    
    if (send(session->socket, buffer, bytes_written, 0) < 0) {
        return -1;
    }
    
    return 0;
}