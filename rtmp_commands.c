#include "rtmp_commands.h"
#include "rtmp_utils.h"
#include "rtmp_diagnostics.h"
#include <string.h>
#include <stdlib.h>

// RTMP command constants
#define RTMP_CMD_CONNECT        "connect"
#define RTMP_CMD_CREATESTREAM   "createStream"
#define RTMP_CMD_PUBLISH        "publish"
#define RTMP_CMD_PLAY          "play"
#define RTMP_CMD_PAUSE         "pause"
#define RTMP_CMD_SEEK          "seek"
#define RTMP_CMD_CLOSE         "closeStream"
#define RTMP_CMD_RELEASE       "releaseStream"
#define RTMP_CMD_RESULT        "_result"
#define RTMP_CMD_ERROR         "_error"
#define RTMP_CMD_ONSTATUS      "onStatus"

// Internal structures
typedef struct {
    double transaction_id;
    rtmp_command_callback callback;
    void *user_data;
} rtmp_command_handler_t;

typedef struct {
    rtmp_command_handler_t *handlers;
    size_t handler_count;
    size_t handler_capacity;
    double next_transaction_id;
    pthread_mutex_t mutex;
} rtmp_command_context_t;

// Initialize command system
rtmp_command_context_t* rtmp_command_init(void) {
    rtmp_command_context_t *ctx = rtmp_utils_malloc(sizeof(rtmp_command_context_t));
    if (!ctx) {
        rtmp_log_error("Failed to allocate command context");
        return NULL;
    }
    
    ctx->handlers = NULL;
    ctx->handler_count = 0;
    ctx->handler_capacity = 0;
    ctx->next_transaction_id = 1.0;
    
    if (pthread_mutex_init(&ctx->mutex, NULL) != 0) {
        rtmp_utils_free(ctx);
        rtmp_log_error("Failed to initialize command mutex");
        return NULL;
    }
    
    return ctx;
}

// Register command handler
double rtmp_command_register_handler(rtmp_command_context_t *ctx,
                                   rtmp_command_callback callback,
                                   void *user_data) {
    if (!ctx || !callback) return -1;
    
    pthread_mutex_lock(&ctx->mutex);
    
    // Check if we need to grow the handler array
    if (ctx->handler_count >= ctx->handler_capacity) {
        size_t new_capacity = ctx->handler_capacity == 0 ? 16 : ctx->handler_capacity * 2;
        rtmp_command_handler_t *new_handlers = rtmp_utils_realloc(ctx->handlers,
            new_capacity * sizeof(rtmp_command_handler_t));
            
        if (!new_handlers) {
            pthread_mutex_unlock(&ctx->mutex);
            rtmp_log_error("Failed to allocate command handlers");
            return -1;
        }
        
        ctx->handlers = new_handlers;
        ctx->handler_capacity = new_capacity;
    }
    
    // Add new handler
    double transaction_id = ctx->next_transaction_id++;
    ctx->handlers[ctx->handler_count].transaction_id = transaction_id;
    ctx->handlers[ctx->handler_count].callback = callback;
    ctx->handlers[ctx->handler_count].user_data = user_data;
    ctx->handler_count++;
    
    pthread_mutex_unlock(&ctx->mutex);
    
    return transaction_id;
}

// Send connect command
int rtmp_command_connect(rtmp_connection_t *conn, const char *app,
                        const rtmp_connect_params_t *params,
                        rtmp_command_callback callback,
                        void *user_data) {
    if (!conn || !app) return RTMP_ERROR_INVALID_PARAM;
    
    // Register callback
    double transaction_id = rtmp_command_register_handler(conn->command_ctx,
                                                        callback, user_data);
    if (transaction_id < 0) {
        return RTMP_ERROR_HANDLER;
    }
    
    // Prepare command object
    rtmp_amf_object_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    
    rtmp_amf_add_string(&cmd, "app", app);
    rtmp_amf_add_string(&cmd, "flashVer", "FMLE/3.0");
    rtmp_amf_add_string(&cmd, "swfUrl", params ? params->swf_url : "");
    rtmp_amf_add_string(&cmd, "tcUrl", params ? params->tc_url : "");
    rtmp_amf_add_bool(&cmd, "fpad", false);
    rtmp_amf_add_number(&cmd, "capabilities", 15.0);
    rtmp_amf_add_number(&cmd, "audioCodecs", 4071.0);
    rtmp_amf_add_number(&cmd, "videoCodecs", 252.0);
    rtmp_amf_add_number(&cmd, "videoFunction", 1.0);
    
    // Add optional parameters
    if (params) {
        if (params->page_url)
            rtmp_amf_add_string(&cmd, "pageUrl", params->page_url);
        if (params->object_encoding)
            rtmp_amf_add_number(&cmd, "objectEncoding", params->object_encoding);
    }
    
    // Send command
    int result = rtmp_command_send(conn, RTMP_CMD_CONNECT, transaction_id, &cmd);
    
    rtmp_amf_free(&cmd);
    return result;
}

// Send createStream command
int rtmp_command_create_stream(rtmp_connection_t *conn,
                             rtmp_command_callback callback,
                             void *user_data) {
    if (!conn) return RTMP_ERROR_INVALID_PARAM;
    
    double transaction_id = rtmp_command_register_handler(conn->command_ctx,
                                                        callback, user_data);
    if (transaction_id < 0) {
        return RTMP_ERROR_HANDLER;
    }
    
    return rtmp_command_send(conn, RTMP_CMD_CREATESTREAM, transaction_id, NULL);
}

// Send publish command
int rtmp_command_publish(rtmp_connection_t *conn, double stream_id,
                        const char *name, const char *type,
                        rtmp_command_callback callback,
                        void *user_data) {
    if (!conn || !name || !type) return RTMP_ERROR_INVALID_PARAM;
    
    double transaction_id = rtmp_command_register_handler(conn->command_ctx,
                                                        callback, user_data);
    if (transaction_id < 0) {
        return RTMP_ERROR_HANDLER;
    }
    
    // Prepare command object
    rtmp_amf_object_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    
    rtmp_amf_add_string(&cmd, "name", name);
    rtmp_amf_add_string(&cmd, "type", type);
    
    // Send command
    int result = rtmp_command_send_to_stream(conn, RTMP_CMD_PUBLISH,
                                           transaction_id, stream_id, &cmd);
    
    rtmp_amf_free(&cmd);
    return result;
}

// Send play command
int rtmp_command_play(rtmp_connection_t *conn, double stream_id,
                     const char *name, double start, double duration,
                     rtmp_command_callback callback,
                     void *user_data) {
    if (!conn || !name) return RTMP_ERROR_INVALID_PARAM;
    
    double transaction_id = rtmp_command_register_handler(conn->command_ctx,
                                                        callback, user_data);
    if (transaction_id < 0) {
        return RTMP_ERROR_HANDLER;
    }
    
    // Prepare command object
    rtmp_amf_object_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    
    rtmp_amf_add_string(&cmd, "name", name);
    rtmp_amf_add_number(&cmd, "start", start);
    rtmp_amf_add_number(&cmd, "duration", duration);
    
    // Send command
    int result = rtmp_command_send_to_stream(conn, RTMP_CMD_PLAY,
                                           transaction_id, stream_id, &cmd);
    
    rtmp_amf_free(&cmd);
    return result;
}

// Handle incoming command
int rtmp_command_handle(rtmp_connection_t *conn, const char *command_name,
                       double transaction_id, const rtmp_amf_object_t *command_object,
                       const rtmp_amf_object_t *info_object) {
    if (!conn || !command_name) return RTMP_ERROR_INVALID_PARAM;
    
    rtmp_log_debug("Handling command: %s (tid: %.0f)", command_name, transaction_id);
    
    // Handle _result and _error responses
    if (strcmp(command_name, RTMP_CMD_RESULT) == 0 ||
        strcmp(command_name, RTMP_CMD_ERROR) == 0) {
        pthread_mutex_lock(&conn->command_ctx->mutex);
        
        // Find matching handler
        for (size_t i = 0; i < conn->command_ctx->handler_count; i++) {
            if (conn->command_ctx->handlers[i].transaction_id == transaction_id) {
                rtmp_command_callback callback = conn->command_ctx->handlers[i].callback;
                void *user_data = conn->command_ctx->handlers[i].user_data;
                
                // Remove handler
                if (i < conn->command_ctx->handler_count - 1) {
                    memmove(&conn->command_ctx->handlers[i],
                           &conn->command_ctx->handlers[i + 1],
                           (conn->command_ctx->handler_count - i - 1) * 
                           sizeof(rtmp_command_handler_t));
                }
                conn->command_ctx->handler_count--;
                
                pthread_mutex_unlock(&conn->command_ctx->mutex);
                
                // Call handler
                if (callback) {
                    return callback(conn, command_name, command_object, 
                                  info_object, user_data);
                }
                return RTMP_SUCCESS;
            }
        }
        
        pthread_mutex_unlock(&conn->command_ctx->mutex);
        rtmp_log_warning("No handler found for transaction ID: %.0f", transaction_id);
        return RTMP_ERROR_NO_HANDLER;
    }
    
    // Handle onStatus
    if (strcmp(command_name, RTMP_CMD_ONSTATUS) == 0) {
        if (conn->status_callback) {
            return conn->status_callback(conn, info_object, conn->status_callback_data);
        }
        return RTMP_SUCCESS;
    }
    
    // Handle other commands through command callback
    if (conn->command_callback) {
        return conn->command_callback(conn, command_name, command_object,
                                    info_object, conn->command_callback_data);
    }
    
    rtmp_log_warning("Unhandled command: %s", command_name);
    return RTMP_ERROR_UNHANDLED_COMMAND;
}

// Set command callback
void rtmp_command_set_callback(rtmp_connection_t *conn,
                             rtmp_command_callback callback,
                             void *user_data) {
    if (!conn) return;
    conn->command_callback = callback;
    conn->command_callback_data = user_data;
}

// Set status callback
void rtmp_command_set_status_callback(rtmp_connection_t *conn,
                                    rtmp_status_callback callback,
                                    void *user_data) {
    if (!conn) return;
    conn->status_callback = callback;
    conn->status_callback_data = user_data;
}

// Cleanup command system
void rtmp_command_cleanup(rtmp_command_context_t *ctx) {
    if (!ctx) return;
    
    pthread_mutex_lock(&ctx->mutex);
    rtmp_utils_free(ctx->handlers);
    pthread_mutex_unlock(&ctx->mutex);
    
    pthread_mutex_destroy(&ctx->mutex);
    rtmp_utils_free(ctx);
}