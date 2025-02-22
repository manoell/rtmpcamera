#include "rtmp_protocol.h"
#include "rtmp_chunk.h"
#include "rtmp_amf.h"
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>

#define RTMP_VERSION 3
#define RTMP_CHUNK_SIZE 128
#define RTMP_WINDOW_ACK_SIZE (2.5 * 1024 * 1024) // 2.5MB
#define RTMP_DEFAULT_TIMEOUT 5000 // 5 segundos
#define RTMP_MAX_RETRY 3

typedef struct {
    uint32_t timestamp;
    uint32_t messageLength;
    uint8_t messageTypeId;
    uint32_t messageStreamId;
    uint8_t *payload;
} RTMPMessage;

typedef struct {
    uint32_t chunkSize;
    uint32_t windowAckSize;
    uint32_t bytesReceived;
    uint32_t lastAckBytes;
    uint64_t lastAckTime;
    uint8_t audioCodec;
    uint8_t videoCodec;
    uint32_t streamId;
    char *appName;
    char *streamName;
    RTMPSessionState state;
    uint32_t retryCount;
} RTMPProtocolContext;

static void rtmp_message_free(RTMPMessage *msg) {
    if (msg) {
        free(msg->payload);
        free(msg);
    }
}

static int send_error(RTMPSession *session, const char *code, const char *level, const char *desc) {
    uint8_t response[1024];
    size_t offset = 0;
    
    // Codificar mensagem de erro em AMF
    offset += amf_encode_string(response + offset, "onStatus");
    offset += amf_encode_number(response + offset, 0);
    offset += amf_encode_null(response + offset);
    
    offset += amf_encode_object_start(response + offset);
    offset += amf_encode_named_string(response + offset, "level", level);
    offset += amf_encode_named_string(response + offset, "code", code);
    offset += amf_encode_named_string(response + offset, "description", desc);
    offset += amf_encode_object_end(response + offset);
    
    return rtmp_send_message(session, RTMP_MSG_AMF0_COMMAND, 0, response, offset);
}

static int handle_connect(RTMPSession *session, RTMPChunk *chunk) {
    RTMPProtocolContext *ctx = session->protocol;
    AMFObject *obj = amf_decode(chunk->payload, chunk->header.messageLength);
    if (!obj) return -1;
    
    const char *app = amf_object_get_string(obj, "app");
    const char *tcUrl = amf_object_get_string(obj, "tcUrl");
    
    if (!app || !tcUrl) {
        amf_object_free(obj);
        return send_error(session, "NetConnection.Connect.Failed", 
                         "error", "Invalid connect parameters");
    }
    
    // Armazenar informações da conexão
    ctx->appName = strdup(app);
    
    // Preparar resposta
    uint8_t response[1024];
    size_t offset = 0;
    
    // Window Acknowledgement Size
    uint8_t ackSize[4];
    uint32_t windowSize = RTMP_WINDOW_ACK_SIZE;
    ackSize[0] = (windowSize >> 24) & 0xFF;
    ackSize[1] = (windowSize >> 16) & 0xFF;
    ackSize[2] = (windowSize >> 8) & 0xFF;
    ackSize[3] = windowSize & 0xFF;
    rtmp_send_message(session, RTMP_MSG_WINDOW_ACK_SIZE, 0, ackSize, 4);
    
    // Set Peer Bandwidth
    uint8_t peerBw[5];
    peerBw[0] = (windowSize >> 24) & 0xFF;
    peerBw[1] = (windowSize >> 16) & 0xFF;
    peerBw[2] = (windowSize >> 8) & 0xFF;
    peerBw[3] = windowSize & 0xFF;
    peerBw[4] = 2; // Dynamic
    rtmp_send_message(session, RTMP_MSG_SET_PEER_BW, 0, peerBw, 5);
    
    // Set Chunk Size
    uint8_t chunkSizeMsg[4];
    uint32_t newChunkSize = RTMP_CHUNK_SIZE;
    chunkSizeMsg[0] = (newChunkSize >> 24) & 0xFF;
    chunkSizeMsg[1] = (newChunkSize >> 16) & 0xFF;
    chunkSizeMsg[2] = (newChunkSize >> 8) & 0xFF;
    chunkSizeMsg[3] = newChunkSize & 0xFF;
    rtmp_send_message(session, RTMP_MSG_SET_CHUNK_SIZE, 0, chunkSizeMsg, 4);
    
    ctx->chunkSize = newChunkSize;
    
    // _result
    offset += amf_encode_string(response + offset, "_result");
    offset += amf_encode_number(response + offset, 1); // transaction ID
    
    // Properties
    offset += amf_encode_object_start(response + offset);
    offset += amf_encode_named_string(response + offset, "fmsVer", "FMS/3,0,1,123");
    offset += amf_encode_named_number(response + offset, "capabilities", 31);
    offset += amf_encode_object_end(response + offset);
    
    // Information
    offset += amf_encode_object_start(response + offset);
    offset += amf_encode_named_string(response + offset, "level", "status");
    offset += amf_encode_named_string(response + offset, "code", "NetConnection.Connect.Success");
    offset += amf_encode_named_string(response + offset, "description", "Connection succeeded.");
    offset += amf_encode_named_string(response + offset, "objectEncoding", "0");
    offset += amf_encode_object_end(response + offset);
    
    int result = rtmp_send_message(session, RTMP_MSG_AMF0_COMMAND, 0, response, offset);
    
    amf_object_free(obj);
    
    if (result == 0) {
        ctx->state = RTMP_SESSION_CONNECTED;
    }
    
    return result;
}

static int handle_create_stream(RTMPSession *session, RTMPChunk *chunk) {
    RTMPProtocolContext *ctx = session->protocol;
    AMFObject *obj = amf_decode(chunk->payload, chunk->header.messageLength);
    if (!obj) return -1;
    
    double transactionId = amf_object_get_number(obj, "transactionId");
    
    // Gerar novo stream ID
    ctx->streamId = session->nextStreamId++;
    
    // Resposta
    uint8_t response[128];
    size_t offset = 0;
    
    offset += amf_encode_string(response + offset, "_result");
    offset += amf_encode_number(response + offset, transactionId);
    offset += amf_encode_null(response + offset);
    offset += amf_encode_number(response + offset, ctx->streamId);
    
    int result = rtmp_send_message(session, RTMP_MSG_AMF0_COMMAND, 0, response, offset);
    
    amf_object_free(obj);
    return result;
}

static int handle_publish(RTMPSession *session, RTMPChunk *chunk) {
    RTMPProtocolContext *ctx = session->protocol;
    AMFObject *obj = amf_decode(chunk->payload, chunk->header.messageLength);
    if (!obj) return -1;
    
    const char *streamName = amf_object_get_string(obj, "name");
    const char *type = amf_object_get_string(obj, "type");
    
    if (!streamName || !type) {
        amf_object_free(obj);
        return send_error(session, "NetStream.Publish.BadName",
                         "error", "Invalid stream name or type");
    }
    
    ctx->streamName = strdup(streamName);
    
    // Notificar sucesso
    int result = send_error(session, "NetStream.Publish.Start",
                          "status", "Stream is now published.");
    
    amf_object_free(obj);
    
    if (result == 0) {
        ctx->state = RTMP_SESSION_PUBLISHING;
    }
    
    return result;
}

static int handle_play(RTMPSession *session, RTMPChunk *chunk) {
    RTMPProtocolContext *ctx = session->protocol;
    AMFObject *obj = amf_decode(chunk->payload, chunk->header.messageLength);
    if (!obj) return -1;
    
    const char *streamName = amf_object_get_string(obj, "name");
    if (!streamName) {
        amf_object_free(obj);
        return send_error(session, "NetStream.Play.Failed",
                         "error", "Invalid stream name");
    }
    
    ctx->streamName = strdup(streamName);
    
    // Stream Begin
    uint8_t begin[6] = {0, 0, 0, 0, 0, 0};
    rtmp_send_message(session, RTMP_MSG_USER_CONTROL, 0, begin, 6);
    
    // Notificar sucesso
    int result = send_error(session, "NetStream.Play.Start",
                          "status", "Stream is now playing.");
    
    amf_object_free(obj);
    
    if (result == 0) {
        ctx->state = RTMP_SESSION_PLAYING;
    }
    
    return result;
}

static int handle_video(RTMPSession *session, RTMPChunk *chunk) {
    if (session->videoCallback) {
        session->videoCallback(session, chunk->payload, 
                             chunk->header.messageLength,
                             chunk->header.timestamp);
    }
    return 0;
}

static int handle_audio(RTMPSession *session, RTMPChunk *chunk) {
    if (session->audioCallback) {
        session->audioCallback(session, chunk->payload,
                             chunk->header.messageLength,
                             chunk->header.timestamp);
    }
    return 0;
}

static int check_window_ack(RTMPSession *session) {
    RTMPProtocolContext *ctx = session->protocol;
    
    ctx->bytesReceived += ctx->chunkSize;
    
    if (ctx->bytesReceived - ctx->lastAckBytes >= ctx->windowAckSize) {
        uint8_t ack[4];
        uint32_t bytesReceived = ctx->bytesReceived;
        ack[0] = (bytesReceived >> 24) & 0xFF;
        ack[1] = (bytesReceived >> 16) & 0xFF;
        ack[2] = (bytesReceived >> 8) & 0xFF;
        ack[3] = bytesReceived & 0xFF;
        
        int result = rtmp_send_message(session, RTMP_MSG_ACKNOWLEDGEMENT, 
                                     0, ack, 4);
        
        if (result == 0) {
            ctx->lastAckBytes = ctx->bytesReceived;
            ctx->lastAckTime = time(NULL);
        }
        
        return result;
    }
    
    return 0;
}

int rtmp_protocol_init(RTMPSession *session) {
    RTMPProtocolContext *ctx = calloc(1, sizeof(RTMPProtocolContext));
    if (!ctx) return -1;
    
    ctx->chunkSize = RTMP_CHUNK_SIZE;
    ctx->windowAckSize = RTMP_WINDOW_ACK_SIZE;
    ctx->state = RTMP_SESSION_INITIALIZED;
    
    session->protocol = ctx;
    return 0;
}

void rtmp_protocol_destroy(RTMPSession *session) {
    if (!session || !session->protocol) return;
    
    RTMPProtocolContext *ctx = session->protocol;
    
    free(ctx->appName);
    free(ctx->streamName);
    free(ctx);
    
    session->protocol = NULL;
}

int rtmp_protocol_handle_message(RTMPSession *session, RTMPChunk *chunk) {
    RTMPProtocolContext *ctx = session->protocol;
    int result = 0;
    
    // Verificar timeout
    if (ctx->lastAckTime > 0 && 
        time(NULL) - ctx->lastAckTime > RTMP_DEFAULT_TIMEOUT) {
        
        if (ctx->retryCount < RTMP_MAX_RETRY) {
            ctx->retryCount++;
            // Tentar reconexão
            return rtmp_protocol_reconnect(session);
        }
        return -1;
    }
    
    // Processar mensagem
    switch (chunk->header.messageTypeId) {
        case RTMP_MSG_SET_CHUNK_SIZE:
            if (chunk->header.messageLength >= 4) {
                ctx->chunkSize = (chunk->payload[0] << 24) |
                                (chunk->payload[1] << 16) |
                                (chunk->payload[2] << 8) |
                                chunk->payload[3];
            }
            break;
            
        case RTMP_MSG_ACKNOWLEDGEMENT:
            // Processar acknowledgement
            break;
            
        case RTMP_MSG_WINDOW_ACK_SIZE:
            if (chunk->header.messageLength >= 4) {
                ctx->windowAckSize = (chunk->payload[0] << 24) |
                                   (chunk->payload[1] << 16) |
                                   (chunk->payload[2] << 8) |
                                   chunk->payload[3];
            }
            break;
            
        case RTMP_MSG_AMF0_COMMAND:
            // Decodificar nome do comando
            char cmdName[128];
            size_t cmdLen;
            if (amf_decode_string(chunk->payload, &cmdLen, cmdName, sizeof(cmdName)) < 0) {
                return -1;
            }
            
            if (strcmp(cmdName, "connect") == 0) {
                result = handle_connect(session, chunk);
            } else if (strcmp(cmdName, "createStream") == 0) {
                result = handle_create_stream(session, chunk);
            } else if (strcmp(cmdName, "publish") == 0) {
                result = handle_publish(session, chunk);
            } else if (strcmp(cmdName, "play") == 0) {
                result = handle_play(session, chunk);
            }
            break;
            
        case RTMP_MSG_VIDEO:
            result = handle_video(session, chunk);
            break;
            
        case RTMP_MSG_AUDIO:
            result = handle_audio(session, chunk);
            break;
            
        case RTMP_MSG_USER_CONTROL:
            // Processar mensagens de controle do usuário
            if (chunk->header.messageLength >= 2) {
                uint16_t eventType = (chunk->payload[0] << 8) | chunk->payload[1];
                switch (eventType) {
                    case RTMP_USER_PING_REQUEST:
                        // Responder ping
                        if (chunk->header.messageLength >= 6) {
                            uint8_t response[6];
                            memcpy(response, chunk->payload, 6);
                            response[1] = RTMP_USER_PING_RESPONSE;
                            rtmp_send_message(session, RTMP_MSG_USER_CONTROL, 0, response, 6);
                        }
                        break;
                        
                    case RTMP_USER_STREAM_BEGIN:
                    case RTMP_USER_STREAM_EOF:
                    case RTMP_USER_STREAM_DRY:
                    case RTMP_USER_SET_BUFFER:
                        // Processar outros eventos de controle
                        break;
                }
            }
            break;
    }
    
    // Verificar acknowledgment window
    if (result == 0) {
        result = check_window_ack(session);
    }
    
    return result;
}

int rtmp_protocol_reconnect(RTMPSession *session) {
    RTMPProtocolContext *ctx = session->protocol;
    
    // Salvar estado importante
    char *appName = ctx->appName;
    char *streamName = ctx->streamName;
    uint32_t streamId = ctx->streamId;
    
    ctx->appName = NULL;
    ctx->streamName = NULL;
    
    // Resetar contexto
    memset(ctx, 0, sizeof(RTMPProtocolContext));
    
    // Restaurar estado
    ctx->appName = appName;
    ctx->streamName = streamName;
    ctx->streamId = streamId;
    ctx->state = RTMP_SESSION_INITIALIZED;
    
    // Realizar handshake novamente
    if (rtmp_handshake_process(session) < 0) {
        return -1;
    }
    
    // Reconectar
    uint8_t connect[1024];
    size_t offset = 0;
    
    offset += amf_encode_string(connect + offset, "connect");
    offset += amf_encode_number(connect + offset, 1);
    
    offset += amf_encode_object_start(connect + offset);
    offset += amf_encode_named_string(connect + offset, "app", ctx->appName);
    offset += amf_encode_named_string(connect + offset, "flashVer", "FMLE/3.0");
    offset += amf_encode_named_string(connect + offset, "swfUrl", "");
    offset += amf_encode_named_string(connect + offset, "tcUrl", "");
    offset += amf_encode_named_boolean(connect + offset, "fpad", false);
    offset += amf_encode_named_number(connect + offset, "capabilities", 15);
    offset += amf_encode_named_number(connect + offset, "audioCodecs", 0x0FFF);
    offset += amf_encode_named_number(connect + offset, "videoCodecs", 0x0FFF);
    offset += amf_encode_named_number(connect + offset, "videoFunction", 1);
    offset += amf_encode_object_end(connect + offset);
    
    if (rtmp_send_message(session, RTMP_MSG_AMF0_COMMAND, 0, connect, offset) < 0) {
        return -1;
    }
    
    // Recriar stream se necessário
    if (ctx->streamId > 0) {
        uint8_t createStream[128];
        offset = 0;
        
        offset += amf_encode_string(createStream + offset, "createStream");
        offset += amf_encode_number(createStream + offset, 2);
        offset += amf_encode_null(createStream + offset);
        
        if (rtmp_send_message(session, RTMP_MSG_AMF0_COMMAND, 0, createStream, offset) < 0) {
            return -1;
        }
        
        // Republicar/reproduzir stream
        if (ctx->streamName) {
            uint8_t publish[256];
            offset = 0;
            
            if (ctx->state == RTMP_SESSION_PUBLISHING) {
                offset += amf_encode_string(publish + offset, "publish");
                offset += amf_encode_number(publish + offset, 3);
                offset += amf_encode_null(publish + offset);
                offset += amf_encode_string(publish + offset, ctx->streamName);
                offset += amf_encode_string(publish + offset, "live");
                
                if (rtmp_send_message(session, RTMP_MSG_AMF0_COMMAND, ctx->streamId, publish, offset) < 0) {
                    return -1;
                }
            } else if (ctx->state == RTMP_SESSION_PLAYING) {
                offset += amf_encode_string(publish + offset, "play");
                offset += amf_encode_number(publish + offset, 3);
                offset += amf_encode_null(publish + offset);
                offset += amf_encode_string(publish + offset, ctx->streamName);
                
                if (rtmp_send_message(session, RTMP_MSG_AMF0_COMMAND, ctx->streamId, publish, offset) < 0) {
                    return -1;
                }
            }
        }
    }
    
    return 0;
}

RTMPSessionState rtmp_protocol_get_state(RTMPSession *session) {
    if (!session || !session->protocol) return RTMP_SESSION_DISCONNECTED;
    return ((RTMPProtocolContext*)session->protocol)->state;
}

int rtmp_protocol_get_stream_id(RTMPSession *session) {
    if (!session || !session->protocol) return -1;
    return ((RTMPProtocolContext*)session->protocol)->streamId;
}

void rtmp_protocol_get_stats(RTMPSession *session, RTMPProtocolStats *stats) {
    if (!session || !session->protocol || !stats) return;
    
    RTMPProtocolContext *ctx = session->protocol;
    
    stats->chunkSize = ctx->chunkSize;
    stats->windowAckSize = ctx->windowAckSize;
    stats->bytesReceived = ctx->bytesReceived;
    stats->retryCount = ctx->retryCount;
}