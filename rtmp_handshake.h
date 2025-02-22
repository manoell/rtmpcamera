#ifndef RTMP_HANDSHAKE_H
#define RTMP_HANDSHAKE_H

#include "rtmp_core.h"

// Error codes
#define RTMP_ERROR_INVALID_PARAM -1
#define RTMP_ERROR_VERSION -2
#define RTMP_ERROR_HANDSHAKE_C0 -3
#define RTMP_ERROR_HANDSHAKE_C1 -4
#define RTMP_ERROR_HANDSHAKE_C2 -5
#define RTMP_ERROR_HANDSHAKE_S0 -6
#define RTMP_ERROR_HANDSHAKE_S1 -7
#define RTMP_ERROR_HANDSHAKE_S2 -8
#define RTMP_ERROR_HANDSHAKE_C0C1 -9
#define RTMP_ERROR_HANDSHAKE_S0S1 -10
#define RTMP_ERROR_HANDSHAKE_DIGEST -11
#define RTMP_ERROR_MEMORY -12

// Handshake types
typedef enum {
    RTMP_HANDSHAKE_TYPE_PLAIN = 0,   // Plain handshake
    RTMP_HANDSHAKE_TYPE_COMPLEX = 1,  // Complex handshake with digest verification
    RTMP_HANDSHAKE_TYPE_SERVER = 2,   // Server-side handshake
    RTMP_HANDSHAKE_TYPE_CLIENT = 3    // Client-side handshake
} rtmp_handshake_type_t;

// Handshake configuration
typedef struct {
    rtmp_handshake_type_t type;       // Type of handshake to perform
    int timeout_ms;                    // Timeout in milliseconds
    int allow_version_mismatch;        // Allow RTMP version mismatch
    int verify_peer;                   // Verify peer digest
} rtmp_handshake_config_t;

// Main handshake functions
int rtmp_handshake_server(rtmp_connection_t *conn);
int rtmp_handshake_client(rtmp_connection_t *conn);

// Utility functions
int rtmp_handshake_verify_digest(const uint8_t *buffer, size_t size);
int rtmp_handshake_generate_digest(uint8_t *buffer, size_t size);

// Advanced configuration
void rtmp_handshake_set_config(rtmp_connection_t *conn, const rtmp_handshake_config_t *config);
void rtmp_handshake_get_config(rtmp_connection_t *conn, rtmp_handshake_config_t *config);

// Handshake states
typedef enum {
    RTMP_HANDSHAKE_STATE_INIT = 0,
    RTMP_HANDSHAKE_STATE_VERSION_SENT,
    RTMP_HANDSHAKE_STATE_ACK_SENT,
    RTMP_HANDSHAKE_STATE_DONE,
    RTMP_HANDSHAKE_STATE_ERROR
} rtmp_handshake_state_t;

// Get current handshake state
rtmp_handshake_state_t rtmp_handshake_get_state(rtmp_connection_t *conn);

// Diagnostics
typedef struct {
    uint64_t handshake_start_time;
    uint64_t handshake_end_time;
    uint32_t bytes_sent;
    uint32_t bytes_received;
    rtmp_handshake_state_t final_state;
    int error_code;
    char error_message[256];
} rtmp_handshake_stats_t;

// Get handshake statistics
void rtmp_handshake_get_stats(rtmp_connection_t *conn, rtmp_handshake_stats_t *stats);

#endif // RTMP_HANDSHAKE_H