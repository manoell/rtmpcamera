#ifndef RTMP_HANDSHAKE_H
#define RTMP_HANDSHAKE_H

#include <stdint.h>

// RTMP Handshake states
typedef enum {
    RTMP_HANDSHAKE_UNINITIALIZED = 0,
    RTMP_HANDSHAKE_0,
    RTMP_HANDSHAKE_1,
    RTMP_HANDSHAKE_2,
    RTMP_HANDSHAKE_DONE
} rtmp_handshake_state_t;

// RTMP Handshake versions
#define RTMP_HANDSHAKE_VERSION_OLD 0x03
#define RTMP_HANDSHAKE_VERSION_NEW 0x06

// Handshake packet sizes
#define RTMP_HANDSHAKE_PACKET_SIZE 1536
#define RTMP_HANDSHAKE_VERSION_SIZE 1

// Function prototypes
int rtmp_handshake_server(int socket);
int rtmp_handshake_client(int socket);
int rtmp_handshake_verify(const uint8_t* data, size_t len);

#endif // RTMP_HANDSHAKE_H