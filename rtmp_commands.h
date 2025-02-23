#ifndef RTMP_COMMANDS_H
#define RTMP_COMMANDS_H

#include <stdint.h>
#include "rtmp_core.h"

// Tipos de comandos RTMP
typedef enum {
    RTMP_CMD_CONNECT = 1,
    RTMP_CMD_CREATESTREAM,
    RTMP_CMD_PUBLISH,
    RTMP_CMD_PLAY,
    RTMP_CMD_PAUSE,
    RTMP_CMD_SEEK,
    RTMP_CMD_CLOSESTREAM,
    RTMP_CMD_DELETESTREAM,
    RTMP_CMD_RESULT,
    RTMP_CMD_ERROR,
    RTMP_CMD_PING,
    RTMP_CMD_PONG,
    RTMP_CMD_CUSTOM
} rtmp_command_type_t;

// Estrutura do comando
typedef struct {
    rtmp_command_type_t type;
    uint32_t transaction_id;
    char *command_name;
    void *command_object;
    void *optional_args;
    size_t optional_args_size;
} rtmp_command_t;

// Funções de criação de comandos
rtmp_command_t* rtmp_command_create(rtmp_command_type_t type);
void rtmp_command_destroy(rtmp_command_t *cmd);

// Funções de codificação/decodificação
int rtmp_command_encode(const rtmp_command_t *cmd, uint8_t *buffer, size_t *size);
int rtmp_command_decode(const uint8_t *buffer, size_t size, rtmp_command_t *cmd);

// Funções de comandos específicos
int rtmp_command_connect(rtmp_command_t *cmd, const char *app_name, const char *tc_url);
int rtmp_command_create_stream(rtmp_command_t *cmd, uint32_t transaction_id);
int rtmp_command_publish(rtmp_command_t *cmd, const char *stream_name, const char *stream_type);
int rtmp_command_play(rtmp_command_t *cmd, const char *stream_name, double start, double duration);
int rtmp_command_pause(rtmp_command_t *cmd, int pause_state, double milliseconds);
int rtmp_command_seek(rtmp_command_t *cmd, double milliseconds);
int rtmp_command_close_stream(rtmp_command_t *cmd, uint32_t stream_id);
int rtmp_command_delete_stream(rtmp_command_t *cmd, uint32_t stream_id);

// Funções de resultado/erro
int rtmp_command_send_result(rtmp_command_t *cmd, uint32_t transaction_id, void *result);
int rtmp_command_send_error(rtmp_command_t *cmd, uint32_t transaction_id, const char *error);

// Funções de controle
int rtmp_command_handle(rtmp_connection_t *conn, const rtmp_command_t *cmd);
int rtmp_command_process(rtmp_connection_t *conn, const uint8_t *data, size_t size);

// Funções de notificação
int rtmp_command_send_status(rtmp_connection_t *conn, const char *level, const char *code, const char *description);
int rtmp_command_send_metadata(rtmp_connection_t *conn, const char *name, const void *data, size_t size);

#endif // RTMP_COMMANDS_H