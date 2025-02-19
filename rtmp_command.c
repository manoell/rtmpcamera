#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rtmp_command.h"
#include "rtmp_amf.h"
#include "rtmp_packet.h"
#include "rtmp_session.h"
#include "rtmp_log.h"

int rtmp_process_command(rtmp_session_t* session, rtmp_packet_t* packet) {
    if (!session || !packet || !packet->data) return -1;

    char command_name[128] = {0};
    if (amf_decode_string(packet->data, packet->data_size, command_name, sizeof(command_name)) < 0) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao decodificar comando");
        return -1;
    }

    log_rtmp_level(RTMP_LOG_INFO, "Comando recebido: %s", command_name);

    if (strcmp(command_name, "connect") == 0) {
        return rtmp_handle_connect(session, packet);
    } else if (strcmp(command_name, "createStream") == 0) {
        return rtmp_handle_create_stream(session, packet);
    } else if (strcmp(command_name, "publish") == 0) {
        return rtmp_handle_publish(session, packet);
    } else if (strcmp(command_name, "play") == 0) {
        return rtmp_handle_play(session, packet);
    } else if (strcmp(command_name, "pause") == 0) {
        return rtmp_handle_pause(session, packet);
    } else if (strcmp(command_name, "seek") == 0) {
        return rtmp_handle_seek(session, packet);
    } else if (strcmp(command_name, "deleteStream") == 0) {
        return rtmp_handle_delete_stream(session, packet);
    }

    log_rtmp_level(RTMP_LOG_WARN, "Comando não implementado: %s", command_name);
    return 0;
}

int rtmp_handle_connect(rtmp_session_t* session, rtmp_packet_t* packet) {
    log_rtmp_level(RTMP_LOG_INFO, "Processando comando connect");
    
    // Enviar Window Acknowledgement Size
    rtmp_send_control_packet(session, RTMP_MSG_WINDOW_ACK_SIZE, session->window_size);

    // Enviar Set Peer Bandwidth
    uint8_t peer_bw[5] = {
    	(session->window_size >> 24) & 0xFF,
    	(session->window_size >> 16) & 0xFF,
    	(session->window_size >> 8) & 0xFF,
    	session->window_size & 0xFF,
    	2  // Dynamic
    };
    rtmp_send_control_packet(session, RTMP_MSG_SET_PEER_BW, (peer_bw[4] << 24) | (peer_bw[3] << 16) | (peer_bw[2] << 8) | peer_bw[1]);

    // Enviar resultado do connect
    rtmp_send_connect_result(session);

    // Enviar Stream Begin
    rtmp_send_stream_begin(session);

    log_rtmp_level(RTMP_LOG_INFO, "Connect processado com sucesso");
    return 0;
}

int rtmp_handle_create_stream(rtmp_session_t* session, rtmp_packet_t* packet) {
    log_rtmp_level(RTMP_LOG_INFO, "Processando createStream");
    
    double transaction_id = 0;
    if (amf_decode_number(packet->data + 1, packet->data_size - 1, &transaction_id) < 0) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao decodificar transaction_id");
        return -1;
    }

    int stream_id = rtmp_create_stream_id(session);
    if (stream_id < 0) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao criar stream");
        return -1;
    }

    rtmp_send_create_stream_result(session, transaction_id, stream_id);

    log_rtmp_level(RTMP_LOG_INFO, "CreateStream processado, stream_id: %d", stream_id);
    return 0;
}

int rtmp_handle_publish(rtmp_session_t* session, rtmp_packet_t* packet) {
    log_rtmp_level(RTMP_LOG_INFO, "Processando publish");
    
    // Enviar Stream Begin
    rtmp_send_stream_begin(session);

    // Definir estado como streaming
    session->state = RTMP_STATE_STREAMING;

    // Enviar onStatus - NetStream.Publish.Start
    uint8_t status[512];
    int offset = 0;

    // AMF String: "onStatus"
    status[offset++] = 0x02; // String marker
    status[offset++] = 0x00; // String length (2 bytes)
    status[offset++] = 0x08;
    memcpy(status + offset, "onStatus", 8);
    offset += 8;

    // Transaction ID (number)
    status[offset++] = 0x00; // Number marker
    memcpy(status + offset, "\x00\x00\x00\x00\x00\x00\x00\x00", 8); // 0.0
    offset += 8;

    // Info object
    status[offset++] = 0x03; // Object marker
    
    // "code" property
    status[offset++] = 0x00; // Property name length (2 bytes)
    status[offset++] = 0x04;
    memcpy(status + offset, "code", 4);
    offset += 4;
    
    status[offset++] = 0x02; // String marker
    status[offset++] = 0x00; // String length (2 bytes)
    status[offset++] = 0x15;
    memcpy(status + offset, "NetStream.Publish.Start", 21);
    offset += 21;

    // "level" property
    status[offset++] = 0x00; // Property name length (2 bytes)
    status[offset++] = 0x05;
    memcpy(status + offset, "level", 5);
    offset += 5;
    
    status[offset++] = 0x02; // String marker
    status[offset++] = 0x00; // String length (2 bytes)
    status[offset++] = 0x06;
    memcpy(status + offset, "status", 6);
    offset += 6;

    status[offset++] = 0x00; // Object end marker
    status[offset++] = 0x00;
    status[offset++] = 0x09;

    rtmp_packet_t response = {0};
    response.type = RTMP_MSG_AMF_COMMAND;
    response.data = status;
    response.data_size = offset;
    rtmp_packet_send(session, &response);

    log_rtmp_level(RTMP_LOG_INFO, "Publish processado com sucesso");
    return 0;
}

int rtmp_handle_play(rtmp_session_t* session, rtmp_packet_t* packet) {
    log_rtmp_level(RTMP_LOG_INFO, "Processando play");
    
    // Enviar Stream Begin
    rtmp_send_stream_begin(session);

    // Definir estado como streaming
    session->state = RTMP_STATE_STREAMING;

    // Enviar onStatus - NetStream.Play.Start
    uint8_t status[512];
    int offset = 0;

    // AMF String: "onStatus"
    status[offset++] = 0x02;
    status[offset++] = 0x00;
    status[offset++] = 0x08;
    memcpy(status + offset, "onStatus", 8);
    offset += 8;

    // Transaction ID
    status[offset++] = 0x00;
    memcpy(status + offset, "\x00\x00\x00\x00\x00\x00\x00\x00", 8);
    offset += 8;

    // Info object
    status[offset++] = 0x03;
    
    // "code" property
    status[offset++] = 0x00;
    status[offset++] = 0x04;
    memcpy(status + offset, "code", 4);
    offset += 4;
    
    status[offset++] = 0x02;
    status[offset++] = 0x00;
    status[offset++] = 0x14;
    memcpy(status + offset, "NetStream.Play.Start", 20);
    offset += 20;

    // "level" property
    status[offset++] = 0x00;
    status[offset++] = 0x05;
    memcpy(status + offset, "level", 5);
    offset += 5;
    
    status[offset++] = 0x02;
    status[offset++] = 0x00;
    status[offset++] = 0x06;
    memcpy(status + offset, "status", 6);
    offset += 6;

    status[offset++] = 0x00;
    status[offset++] = 0x00;
    status[offset++] = 0x09;

    rtmp_packet_t response = {0};
    response.type = RTMP_MSG_AMF_COMMAND;
    response.data = status;
    response.data_size = offset;
    rtmp_packet_send(session, &response);

    log_rtmp_level(RTMP_LOG_INFO, "Play processado com sucesso");
    return 0;
}

int rtmp_handle_pause(rtmp_session_t* session, rtmp_packet_t* packet) {
    log_rtmp_level(RTMP_LOG_INFO, "Processando pause");
    // TODO: Implementar pause
    return 0;
}

int rtmp_handle_seek(rtmp_session_t* session, rtmp_packet_t* packet) {
    log_rtmp_level(RTMP_LOG_INFO, "Processando seek");
    // TODO: Implementar seek
    return 0;
}

int rtmp_handle_delete_stream(rtmp_session_t* session, rtmp_packet_t* packet) {
    log_rtmp_level(RTMP_LOG_INFO, "Processando deleteStream");
    
    // Limpar o stream correspondente
    double stream_id = 0;
    if (amf_decode_number(packet->data + 1, packet->data_size - 1, &stream_id) < 0) {
        log_rtmp_level(RTMP_LOG_ERROR, "Falha ao decodificar stream_id");
        return -1;
    }

    // Encontrar e desativar o stream
    for (int i = 0; i < session->stream_count; i++) {
        if (session->streams[i].id == (uint32_t)stream_id) {
            session->streams[i].active = 0;
            if (session->streams[i].data) {
                free(session->streams[i].data);
                session->streams[i].data = NULL;
            }
            break;
        }
    }

    log_rtmp_level(RTMP_LOG_INFO, "DeleteStream processado com sucesso");
    return 0;
}

int rtmp_send_connect_result(rtmp_session_t* session) {
    uint8_t result[512];
    int offset = 0;

    // AMF String: "_result"
    result[offset++] = 0x02;
    result[offset++] = 0x00;
    result[offset++] = 0x07;
    memcpy(result + offset, "_result", 7);
    offset += 7;

    // Transaction ID
    result[offset++] = 0x00;
    memcpy(result + offset, "\x3f\xf0\x00\x00\x00\x00\x00\x00", 8);
    offset += 8;

    // Properties object
    result[offset++] = 0x03;
    
    // "fmsVer" property
    result[offset++] = 0x00;
    result[offset++] = 0x06;
    memcpy(result + offset, "fmsVer", 6);
    offset += 6;
    
    result[offset++] = 0x02;
    result[offset++] = 0x00;
    result[offset++] = 0x0D;
    memcpy(result + offset, "FMS/3,0,1,123", 13);
    offset += 13;

    // Object end marker
    result[offset++] = 0x00;
    result[offset++] = 0x00;
    result[offset++] = 0x09;

    rtmp_packet_t response = {0};
    response.type = RTMP_MSG_AMF_COMMAND;
    response.data = result;
    response.data_size = offset;
    return rtmp_packet_send(session, &response);
}

int rtmp_send_stream_begin(rtmp_session_t* session) {
    uint8_t stream_begin[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    rtmp_packet_t response = {0};
    response.type = RTMP_MSG_USER_CONTROL;
    response.data = stream_begin;
    response.data_size = 6;
    return rtmp_packet_send(session, &response);
}

int rtmp_send_create_stream_result(rtmp_session_t* session, double transaction_id, double stream_id) {
    uint8_t result[256];
    int offset = 0;

    // AMF String: "_result"
    result[offset++] = 0x02;
    result[offset++] = 0x00;
    result[offset++] = 0x07;
    memcpy(result + offset, "_result", 7);
    offset += 7;

    // Transaction ID
    result[offset++] = 0x00;
    memcpy(result + offset, &transaction_id, 8);
    offset += 8;

    // NULL object
    result[offset++] = 0x05;

    // Stream ID
    result[offset++] = 0x00;
    memcpy(result + offset, &stream_id, 8);
    offset += 8;

    rtmp_packet_t response = {0};
    response.type = RTMP_MSG_AMF_COMMAND;
    response.data = result;
    response.data_size = offset;
    return rtmp_packet_send(session, &response);
}