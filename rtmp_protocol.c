#include "rtmp_protocol.h"
#include "rtmp_chunk.h"
#include "rtmp_amf.h"
#include "rtmp_utils.h"
#include <string.h>
#include <stdlib.h>

static int rtmp_handle_abort(rtmp_session_t *session, const uint8_t *data, size_t size);
static int rtmp_handle_data(rtmp_session_t *session, const uint8_t *data, size_t size);
static int rtmp_handle_audio(rtmp_session_t *session, const uint8_t *data, size_t size, uint32_t timestamp);
static int rtmp_handle_video(rtmp_session_t *session, const uint8_t *data, size_t size, uint32_t timestamp);
static int rtmp_handle_aggregate(rtmp_session_t *session, const uint8_t *data, size_t size);

int rtmp_process_input(rtmp_session_t *session, const uint8_t *data, size_t size) {
    if (!session || !data || !size) return -1;
    
    size_t offset = 0;
    while (offset < size) {
        rtmp_chunk_t chunk;
        size_t bytes_read = rtmp_chunk_read(session, data + offset, size - offset, &chunk);
        
        if (bytes_read == 0) {
            // Precisa de mais dados
            break;
        }
        
        if (bytes_read == (size_t)-1) {
            // Erro no parsing do chunk
            return -1;
        }
        
        offset += bytes_read;
        
        if (chunk.msg_data) {
            // Processar mensagem completa
            if (rtmp_process_message(session, &chunk) < 0) {
                return -1;
            }
            
            free(chunk.msg_data);
        }
    }
    
    return 0;
}

int rtmp_process_message(rtmp_session_t *session, rtmp_chunk_t *chunk) {
    if (!session || !chunk) return -1;

    switch (chunk->msg_type_id) {
        case RTMP_MSG_SET_CHUNK_SIZE: {
            uint32_t chunk_size;
            memcpy(&chunk_size, chunk->msg_data, 4);
            chunk_size = RTMP_NTOHL(chunk_size);
            session->in_chunk_size = chunk_size;
            return 0;
        }

        case RTMP_MSG_ABORT:
            return rtmp_handle_abort(session, chunk->msg_data, chunk->msg_length);

        case RTMP_MSG_ACKNOWLEDGEMENT: {
            uint32_t sequence_number;
            memcpy(&sequence_number, chunk->msg_data, 4);
            sequence_number = RTMP_NTOHL(sequence_number);
            session->last_ack_received = sequence_number;
            return 0;
        }

        case RTMP_MSG_WINDOW_ACK_SIZE: {
            uint32_t window_size;
            memcpy(&window_size, chunk->msg_data, 4);
            window_size = RTMP_NTOHL(window_size);
            session->window_ack_size = window_size;
            return 0;
        }

        case RTMP_MSG_SET_PEER_BW: {
            uint32_t window_size;
            uint8_t limit_type;
            memcpy(&window_size, chunk->msg_data, 4);
            window_size = RTMP_NTOHL(window_size);
            limit_type = chunk->msg_data[4];
            session->peer_bandwidth = window_size;
            session->peer_bandwidth_limit_type = limit_type;
            return rtmp_send_window_acknowledgement_size(session, window_size);
        }

        case RTMP_MSG_AUDIO:
            return rtmp_handle_audio(session, chunk->msg_data, chunk->msg_length, chunk->timestamp);

        case RTMP_MSG_VIDEO:
            return rtmp_handle_video(session, chunk->msg_data, chunk->msg_length, chunk->timestamp);

        case RTMP_MSG_DATA_AMF3:
            return rtmp_handle_data(session, chunk->msg_data + 1, chunk->msg_length - 1);

        case RTMP_MSG_DATA_AMF0:
            return rtmp_handle_data(session, chunk->msg_data, chunk->msg_length);

        case RTMP_MSG_COMMAND_AMF3:
            return rtmp_handle_command(session, chunk->msg_data + 1, chunk->msg_length - 1);

        case RTMP_MSG_COMMAND_AMF0:
            return rtmp_handle_command(session, chunk->msg_data, chunk->msg_length);

        case RTMP_MSG_AGGREGATE:
            return rtmp_handle_aggregate(session, chunk->msg_data, chunk->msg_length);

        default:
            // Ignore unknown message types
            return 0;
    }
}

static int rtmp_handle_audio(rtmp_session_t *session, const uint8_t *data, size_t size, uint32_t timestamp) {
    if (!session || !data || !size) return -1;

    // Parse AAC audio data
    uint8_t sound_format = (data[0] >> 4) & 0x0F;
    uint8_t sound_rate = (data[0] >> 2) & 0x03;
    uint8_t sound_size = (data[0] >> 1) & 0x01;
    uint8_t sound_type = data[0] & 0x01;
    uint8_t aac_packet_type = data[1];

    // Handle AAC audio
    if (sound_format == 10) { // AAC
        if (aac_packet_type == 0) { // AAC sequence header
            if (session->aac_sequence_header) {
                free(session->aac_sequence_header);
            }
            session->aac_sequence_header = malloc(size - 2);
            if (!session->aac_sequence_header) return -1;
            
            session->aac_sequence_header_size = size - 2;
            memcpy(session->aac_sequence_header, data + 2, size - 2);
        } else { // AAC raw data
            // Process AAC frame
            if (session->audio_callback) {
                session->audio_callback(session, data + 2, size - 2, timestamp);
            }
        }
    }

    return 0;
}

static int rtmp_handle_video(rtmp_session_t *session, const uint8_t *data, size_t size, uint32_t timestamp) {
    if (!session || !data || !size) return -1;

    // Parse H.264 video data
    uint8_t frame_type = (data[0] >> 4) & 0x0F;
    uint8_t codec_id = data[0] & 0x0F;
    uint8_t avc_packet_type = data[1];
    
    // Handle H.264 video
    if (codec_id == 7) { // H.264/AVC
        if (avc_packet_type == 0) { // AVC sequence header
            if (session->avc_sequence_header) {
                free(session->avc_sequence_header);
            }
            session->avc_sequence_header = malloc(size - 5);
            if (!session->avc_sequence_header) return -1;
            
            session->avc_sequence_header_size = size - 5;
            memcpy(session->avc_sequence_header, data + 5, size - 5);
        } else if (avc_packet_type == 1) { // AVC NALU
            // Process H.264 frame
            if (session->video_callback) {
                session->video_callback(session, data + 5, size - 5, timestamp);
            }
        }
    }

    return 0;
}

static int rtmp_handle_aggregate(rtmp_session_t *session, const uint8_t *data, size_t size) {
    if (!session || !data || size < 11) return -1;

    size_t offset = 0;
    while (offset + 11 <= size) {
        uint8_t type = data[offset];
        uint32_t msg_size = RTMP_NTOHL(*(uint32_t*)(data + offset + 1));
        uint32_t timestamp = RTMP_NTOHL(*(uint32_t*)(data + offset + 5));
        uint32_t stream_id = RTMP_NTOHL(*(uint32_t*)(data + offset + 8));

        if (offset + 11 + msg_size > size) break;

        const uint8_t *msg_data = data + offset + 11;
        rtmp_chunk_t chunk = {
            .msg_type_id = type,
            .msg_length = msg_size,
            .timestamp = timestamp,
            .msg_stream_id = stream_id,
            .msg_data = (uint8_t*)msg_data
        };

        rtmp_process_message(session, &chunk);

        offset += 11 + msg_size + 4; // Including back pointer
    }

    return 0;
}

int rtmp_send_message(rtmp_session_t *session, uint8_t msg_type_id, uint32_t msg_stream_id, 
                     const uint8_t *data, size_t size) {
    if (!session || (!data && size > 0)) return -1;

    rtmp_chunk_t chunk = {
        .msg_type_id = msg_type_id,
        .msg_stream_id = msg_stream_id,
        .msg_length = size,
        .timestamp = 0,
        .msg_data = (uint8_t*)data
    };

    return rtmp_chunk_write(session, &chunk);
}

int rtmp_send_user_control(rtmp_session_t *session, uint16_t event_type, uint32_t event_data) {
    uint8_t message[6];
    uint16_t be_event_type = RTMP_HTONS(event_type);
    uint32_t be_event_data = RTMP_HTONL(event_data);

    memcpy(message, &be_event_type, 2);
    memcpy(message + 2, &be_event_data, 4);

    return rtmp_send_message(session, RTMP_MSG_USER_CONTROL, 0, message, sizeof(message));
}

int rtmp_send_window_acknowledgement_size(rtmp_session_t *session, uint32_t window_size) {
    uint32_t be_window_size = RTMP_HTONL(window_size);
    return rtmp_send_message(session, RTMP_MSG_WINDOW_ACK_SIZE, 0, 
                           (uint8_t*)&be_window_size, sizeof(be_window_size));
}

int rtmp_send_set_peer_bandwidth(rtmp_session_t *session, uint32_t window_size, uint8_t limit_type) {
    uint8_t message[5];
    uint32_t be_window_size = RTMP_HTONL(window_size);

    memcpy(message, &be_window_size, 4);
    message[4] = limit_type;

    return rtmp_send_message(session, RTMP_MSG_SET_PEER_BW, 0, message, sizeof(message));
}

int rtmp_send_chunk_size(rtmp_session_t *session, uint32_t chunk_size) {
    uint32_t be_chunk_size = RTMP_HTONL(chunk_size);
    return rtmp_send_message(session, RTMP_MSG_SET_CHUNK_SIZE, 0, 
                           (uint8_t*)&be_chunk_size, sizeof(be_chunk_size));
}

static int rtmp_handle_data(rtmp_session_t *session, const uint8_t *data, size_t size) {
    if (!session || !data || !size) return -1;

    size_t offset = 0;
    char *data_type = NULL;
    
    // Decode data type string
    if (rtmp_amf_decode_string(data, size, &offset, &data_type) < 0) {
        return -1;
    }

    // Handle different data types
    if (strcmp(data_type, "@setDataFrame") == 0) {
        // Process metadata
        char *metadata_type = NULL;
        if (rtmp_amf_decode_string(data, size, &offset, &metadata_type) == 0) {
            if (strcmp(metadata_type, "onMetaData") == 0) {
                // Store metadata for the stream
                if (session->metadata_callback) {
                    session->metadata_callback(session, data + offset, size - offset);
                }
            }
            free(metadata_type);
        }
    }

    free(data_type);
    return 0;
}

static int rtmp_handle_abort(rtmp_session_t *session, const uint8_t *data, size_t size) {
    if (!session || !data || size < 4) return -1;

    uint32_t chunk_stream_id;
    memcpy(&chunk_stream_id, data, 4);
    chunk_stream_id = RTMP_NTOHL(chunk_stream_id);

    // Clear incomplete chunks for this stream
    rtmp_chunk_stream_t *chunk_stream = rtmp_get_chunk_stream(session, chunk_stream_id);
    if (chunk_stream) {
        chunk_stream->msg_length = 0;
        chunk_stream->msg_type_id = 0;
        chunk_stream->msg_stream_id = 0;
        if (chunk_stream->msg_data) {
            free(chunk_stream->msg_data);
            chunk_stream->msg_data = NULL;
        }
    }

    return 0;
}