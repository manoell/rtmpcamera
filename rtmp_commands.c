#include "rtmp_commands.h"
#include "rtmp_amf.h"
#include "rtmp_core.h"
#include "rtmp_utils.h"
#include <string.h>
#include <stdlib.h>

// Command Names
#define RTMP_CMD_CONNECT        "connect"
#define RTMP_CMD_CREATE_STREAM  "createStream"
#define RTMP_CMD_PLAY          "play"
#define RTMP_CMD_PAUSE         "pause"
#define RTMP_CMD_RELEASE       "releaseStream"
#define RTMP_CMD_FC_PUBLISH    "FCPublish"
#define RTMP_CMD_PUBLISH       "publish"
#define RTMP_CMD_DELETE_STREAM "deleteStream"
#define RTMP_CMD_CLOSE        "close"

// Response Status
#define RTMP_STATUS_OK         "NetStream.Play.Start"
#define RTMP_STATUS_STREAM_NOT_FOUND "NetStream.Play.StreamNotFound"
#define RTMP_STATUS_PUBLISH_START "NetStream.Publish.Start"
#define RTMP_STATUS_UNPUBLISH_SUCCESS "NetStream.Unpublish.Success"

typedef struct {
    double transaction_id;
    char *command_name;
    rtmp_amf_t *response;
} rtmp_command_t;

static rtmp_command_t* rtmp_command_create(void) {
    rtmp_command_t *cmd = (rtmp_command_t*)malloc(sizeof(rtmp_command_t));
    if (!cmd) return NULL;
    
    cmd->command_name = NULL;
    cmd->transaction_id = 0;
    cmd->response = rtmp_amf_create();
    
    if (!cmd->response) {
        free(cmd);
        return NULL;
    }
    
    return cmd;
}

static void rtmp_command_destroy(rtmp_command_t *cmd) {
    if (cmd) {
        if (cmd->command_name) free(cmd->command_name);
        if (cmd->response) rtmp_amf_destroy(cmd->response);
        free(cmd);
    }
}

static int rtmp_send_error(rtmp_session_t *session, double transaction_id, 
                          const char *level, const char *code, const char *desc) {
    rtmp_amf_t *amf = rtmp_amf_create();
    if (!amf) return -1;
    
    rtmp_amf_encode_string(amf, "_error");
    rtmp_amf_encode_number(amf, transaction_id);
    rtmp_amf_encode_null(amf);
    
    rtmp_amf_begin_object(amf);
    rtmp_amf_encode_string(amf, "level");
    rtmp_amf_encode_string(amf, level);
    rtmp_amf_encode_string(amf, "code");
    rtmp_amf_encode_string(amf, code);
    rtmp_amf_encode_string(amf, "description");
    rtmp_amf_encode_string(amf, desc);
    rtmp_amf_end_object(amf);
    
    size_t size;
    const uint8_t *data = rtmp_amf_get_data(amf, &size);
    int result = rtmp_send_message(session, RTMP_MSG_AMF0_COMMAND, 0, data, size);
    
    rtmp_amf_destroy(amf);
    return result;
}

static int rtmp_send_result(rtmp_session_t *session, double transaction_id, 
                           const char *command, const char *code, 
                           const char *level, const char *description) {
    rtmp_amf_t *amf = rtmp_amf_create();
    if (!amf) return -1;
    
    rtmp_amf_encode_string(amf, "_result");
    rtmp_amf_encode_number(amf, transaction_id);
    rtmp_amf_encode_null(amf);
    
    rtmp_amf_begin_object(amf);
    if (code) {
        rtmp_amf_encode_string(amf, "code");
        rtmp_amf_encode_string(amf, code);
    }
    if (level) {
        rtmp_amf_encode_string(amf, "level");
        rtmp_amf_encode_string(amf, level);
    }
    if (description) {
        rtmp_amf_encode_string(amf, "description");
        rtmp_amf_encode_string(amf, description);
    }
    rtmp_amf_end_object(amf);
    
    size_t size;
    const uint8_t *data = rtmp_amf_get_data(amf, &size);
    int result = rtmp_send_message(session, RTMP_MSG_AMF0_COMMAND, 0, data, size);
    
    rtmp_amf_destroy(amf);
    return result;
}

int rtmp_handle_connect(rtmp_session_t *session, const uint8_t *payload, size_t size) {
    size_t offset = 0;
    char *command_name = NULL;
    double transaction_id = 0;
    
    if (rtmp_amf_decode_string(payload, size, &offset, &command_name) < 0 ||
        rtmp_amf_decode_number(payload, size, &offset, &transaction_id) < 0) {
        if (command_name) free(command_name);
        return -1;
    }
    
    // Skip properties object parsing for now
    
    // Send window acknowledgement size
    rtmp_send_window_acknowledgement_size(session, RTMP_DEFAULT_WINDOW_SIZE);
    
    // Send set peer bandwidth
    rtmp_send_set_peer_bandwidth(session, RTMP_DEFAULT_WINDOW_SIZE, 2);
    
    // Send stream begin
    rtmp_send_user_control(session, RTMP_USER_STREAM_BEGIN, 0);
    
    // Send connect result
    int result = rtmp_send_result(session, transaction_id, 
                                 "_result",
                                 "NetConnection.Connect.Success",
                                 "status",
                                 "Connection succeeded.");
    
    free(command_name);
    return result;
}

int rtmp_handle_create_stream(rtmp_session_t *session, const uint8_t *payload, size_t size) {
    size_t offset = 0;
    char *command_name = NULL;
    double transaction_id = 0;
    
    if (rtmp_amf_decode_string(payload, size, &offset, &command_name) < 0 ||
        rtmp_amf_decode_number(payload, size, &offset, &transaction_id) < 0) {
        if (command_name) free(command_name);
        return -1;
    }
    
    // Create new stream ID
    uint32_t stream_id = rtmp_session_create_stream(session);
    
    rtmp_amf_t *amf = rtmp_amf_create();
    if (!amf) {
        free(command_name);
        return -1;
    }
    
    rtmp_amf_encode_string(amf, "_result");
    rtmp_amf_encode_number(amf, transaction_id);
    rtmp_amf_encode_null(amf);
    rtmp_amf_encode_number(amf, stream_id);
    
    size_t resp_size;
    const uint8_t *data = rtmp_amf_get_data(amf, &resp_size);
    int result = rtmp_send_message(session, RTMP_MSG_AMF0_COMMAND, 0, data, resp_size);
    
    rtmp_amf_destroy(amf);
    free(command_name);
    return result;
}

int rtmp_handle_publish(rtmp_session_t *session, const uint8_t *payload, size_t size) {
    size_t offset = 0;
    char *command_name = NULL;
    double transaction_id = 0;
    char *stream_name = NULL;
    
    if (rtmp_amf_decode_string(payload, size, &offset, &command_name) < 0 ||
        rtmp_amf_decode_number(payload, size, &offset, &transaction_id) < 0 ||
        rtmp_amf_decode_null(payload, size, &offset) < 0 ||
        rtmp_amf_decode_string(payload, size, &offset, &stream_name) < 0) {
        if (command_name) free(command_name);
        if (stream_name) free(stream_name);
        return -1;
    }
    
    // Setup publish stream
    if (rtmp_session_set_publish_stream(session, stream_name) < 0) {
        rtmp_send_error(session, transaction_id,
                       "error",
                       "NetStream.Publish.BadName",
                       "Stream name already in use.");
        free(command_name);
        free(stream_name);
        return -1;
    }
    
    // Send publish success response
    int result = rtmp_send_result(session, transaction_id,
                                 "onStatus",
                                 RTMP_STATUS_PUBLISH_START,
                                 "status",
                                 "Stream is now published.");
    
    free(command_name);
    free(stream_name);
    return result;
}

int rtmp_handle_play(rtmp_session_t *session, const uint8_t *payload, size_t size) {
    size_t offset = 0;
    char *command_name = NULL;
    double transaction_id = 0;
    char *stream_name = NULL;
    
    if (rtmp_amf_decode_string(payload, size, &offset, &command_name) < 0 ||
        rtmp_amf_decode_number(payload, size, &offset, &transaction_id) < 0 ||
        rtmp_amf_decode_null(payload, size, &offset) < 0 ||
        rtmp_amf_decode_string(payload, size, &offset, &stream_name) < 0) {
        if (command_name) free(command_name);
        if (stream_name) free(stream_name);
        return -1;
    }
    
    // Setup play stream
    if (rtmp_session_set_play_stream(session, stream_name) < 0) {
        rtmp_send_error(session, transaction_id,
                       "error",
                       RTMP_STATUS_STREAM_NOT_FOUND,
                       "Stream not found.");
        free(command_name);
        free(stream_name);
        return -1;
    }
    
    // Send stream begin
    rtmp_send_user_control(session, RTMP_USER_STREAM_BEGIN, session->stream_id);
    
    // Send play reset
    rtmp_send_result(session, 0,
                     "onStatus",
                     "NetStream.Play.Reset",
                     "status",
                     "Playing and resetting stream.");
    
    // Send play start
    int result = rtmp_send_result(session, 0,
                                 "onStatus",
                                 RTMP_STATUS_OK,
                                 "status",
                                 "Started playing stream.");
    
    free(command_name);
    free(stream_name);
    return result;
}

int rtmp_handle_delete_stream(rtmp_session_t *session, const uint8_t *payload, size_t size) {
    size_t offset = 0;
    char *command_name = NULL;
    double transaction_id = 0;
    double stream_id = 0;
    
    if (rtmp_amf_decode_string(payload, size, &offset, &command_name) < 0 ||
        rtmp_amf_decode_number(payload, size, &offset, &transaction_id) < 0 ||
        rtmp_amf_decode_null(payload, size, &offset) < 0 ||
        rtmp_amf_decode_number(payload, size, &offset, &stream_id) < 0) {
        if (command_name) free(command_name);
        return -1;
    }
    
    rtmp_session_delete_stream(session, (uint32_t)stream_id);
    
    free(command_name);
    return 0;
}

int rtmp_handle_command(rtmp_session_t *session, const uint8_t *payload, size_t size) {
    if (!size) return -1;
    
    size_t offset = 0;
    char *command_name = NULL;
    
    if (rtmp_amf_decode_string(payload, size, &offset, &command_name) < 0) {
        return -1;
    }
    
    int result = -1;
    
    if (strcmp(command_name, RTMP_CMD_CONNECT) == 0) {
        result = rtmp_handle_connect(session, payload, size);
    }
    else if (strcmp(command_name, RTMP_CMD_CREATE_STREAM) == 0) {
        result = rtmp_handle_create_stream(session, payload, size);
    }
    else if (strcmp(command_name, RTMP_CMD_PUBLISH) == 0) {
        result = rtmp_handle_publish(session, payload, size);
    }
    else if (strcmp(command_name, RTMP_CMD_PLAY) == 0) {
        result = rtmp_handle_play(session, payload, size);
    }
    else if (strcmp(command_name, RTMP_CMD_DELETE_STREAM) == 0) {
        result = rtmp_handle_delete_stream(session, payload, size);
    }
    // Outros comandos podem ser ignorados por enquanto
    else {
        result = 0;
    }
    
    free(command_name);
    return result;
}