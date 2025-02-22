#ifndef RTMP_HANDSHAKE_H
#define RTMP_HANDSHAKE_H

#include <stdint.h>
#include <stddef.h>
#include "rtmp_core.h"

// Handshake constants
#define RTMP_HANDSHAKE_VERSION     3
#define RTMP_HANDSHAKE_SIZE        1536
#define RTMP_HANDSHAKE_PACKET_SIZE (1 + RTMP_HANDSHAKE_SIZE)

// Handshake types
#define RTMP_HANDSHAKE_TYPE_PLAIN  3
#define RTMP_HANDSHAKE_TYPE_VERIFY 6

// Handshake states
#define RTMP_HANDSHAKE_STATE_UNINITIALIZED 0
#define RTMP_HANDSHAKE_STATE_VERSION_SENT  1
#define RTMP_HANDSHAKE_STATE_ACK_SENT      2
#define RTMP_HANDSHAKE_STATE_DONE          3

// Handshake functions
int rtmp_handshake_server(rtmp_session_t *session);
int rtmp_handshake_client(rtmp_session_t *session);

// Internal handshake steps
int rtmp_handshake_read_version(rtmp_session_t *session);
int rtmp_handshake_write_s0s1(rtmp_session_t *session);
int rtmp_handshake_read_c1(rtmp_session_t *session);
int rtmp_handshake_write_s2(rtmp_session_t *session);
int rtmp_handshake_read_c2(rtmp_session_t *session);

// Utility functions
int rtmp_handshake_generate_random(uint8_t *buffer, size_t size);
int rtmp_handshake_verify_digest(const uint8_t *buffer, size_t size);
uint32_t rtmp_handshake_get_time(void);

#endif // RTMP_HANDSHAKE_H