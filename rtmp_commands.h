#ifndef RTMP_COMMANDS_H
#define RTMP_COMMANDS_H

#include "rtmp_core.h"
#include "rtmp_amf.h"

// Error codes
#define RTMP_ERROR_INVALID_PARAM -1
#define RTMP_ERROR_HANDLER -2
#define RTMP_ERROR_NO_HANDLER -3
#define RTMP_ERROR_UNHANDLED_COMMAND -4
#define RTMP_ERROR_MEMORY -5

// Forward declaration
typedef struct rtmp_command_context rtmp_command_context_t;

// Connect parameters
typedef struct {
    const char *swf_url;
    const char *tc_url;
    const char *page_url;
    double object_encoding;
} rtmp_connect_params_t;

// Callback types
typedef int (*rtmp_command_callback)(rtmp_connection_t *conn,
                                   const char *command_name,
                                   const rtmp_amf_object_t *command_object,
                                   const rtmp_amf_object_t *info_object,
                                   void *user_data);

typedef int (*rtmp_status_callback)(rtmp_connection_t *conn,
                                  const rtmp_amf_object_t *info_object,
                                  void *user_data);

// Initialize/cleanup
rtmp_command_context_t* rtmp_command_init(void);
void rtmp_command_cleanup(rtmp_command_context_t *ctx);

// Command functions
int rtmp_command_connect(rtmp_connection_t *conn,
                        const char *app,
                        const rtmp_connect_params_t *params,
                        rtmp_command_callback callback,
                        void *user_data);

int rtmp_command_create_stream(rtmp_connection_t *conn,
                             rtmp_command_callback callback,
                             void *user_data);

int rtmp_command_publish(rtmp_connection_t *conn,
                        double stream_id,
                        const char *name,
                        const char *type,
                        rtmp_command_callback callback,
                        void *user_data);

int rtmp_command_play(rtmp_connection_t *conn,
                     double stream_id,
                     const char *name,
                     double start,
                     double duration,
                     rtmp_command_callback callback,
                     void *user_data);

int rtmp_command_pause(rtmp_connection_t *conn,
                      double stream_id,
                      bool pause,
                      double milliseconds,
                      rtmp_command_callback callback,
                      void *user_data);

int rtmp_command_seek(rtmp_connection_t *conn,
                     double stream_id,
                     double milliseconds,
                     rtmp_command_callback callback,
                     void *user_data);

int rtmp_command_close_stream(rtmp_connection_t *conn,
                            double stream_id,
                            rtmp_command_callback callback,
                            void *user_data);

int rtmp_command_release_stream(rtmp_connection_t *conn,
                              const char *name,
                              rtmp_command_callback callback,
                              void *user_data);

// Command handling
int rtmp_command_handle(rtmp_connection_t *conn,
                       const char *command_name,
                       double transaction_id,
                       const rtmp_amf_object_t *command_object,
                       const rtmp_amf_object_t *info_object);

// Callback registration
void rtmp_command_set_callback(rtmp_connection_t *conn,
                             rtmp_command_callback callback,
                             void *user_data);

void rtmp_command_set_status_callback(rtmp_connection_t *conn,
                                    rtmp_status_callback callback,
                                    void *user_data);

// Command statistics
typedef struct {
    uint32_t commands_sent;
    uint32_t commands_received;
    uint32_t errors;
    uint32_t timeouts;
    double average_response_time;
} rtmp_command_stats_t;

// Get command statistics
void rtmp_command_get_stats(rtmp_connection_t *conn,
                          rtmp_command_stats_t *stats);

// Command debugging
typedef struct {
    bool log_commands;           // Log all commands
    bool log_responses;          // Log all responses
    bool log_transactions;       // Log transaction IDs
    bool dump_amf;              // Dump AMF objects
    int log_level;              // Debug log level
} rtmp_command_debug_t;

// Set debug options
void rtmp_command_set_debug(rtmp_connection_t *conn,
                          const rtmp_command_debug_t *debug);

// Transaction tracking
typedef struct {
    double transaction_id;
    char command_name[32];
    uint64_t timestamp;
    uint32_t retries;
    bool completed;
} rtmp_transaction_info_t;

// Get active transactions
int rtmp_command_get_transactions(rtmp_connection_t *conn,
                                rtmp_transaction_info_t *transactions,
                                size_t *count);

// Command queue management
int rtmp_command_queue_size(rtmp_connection_t *conn);
void rtmp_command_queue_clear(rtmp_connection_t *conn);
int rtmp_command_queue_flush(rtmp_connection_t *conn);

// Advanced options
typedef struct {
    uint32_t retry_count;        // Max retries for failed commands
    uint32_t retry_delay;        // Delay between retries (ms)
    uint32_t timeout;           // Command timeout (ms)
    uint32_t queue_size;        // Maximum command queue size
    bool auto_retry;            // Auto retry failed commands
} rtmp_command_options_t;

// Set command options
void rtmp_command_set_options(rtmp_connection_t *conn,
                            const rtmp_command_options_t *options);

#endif // RTMP_COMMANDS_H