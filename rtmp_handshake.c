#include "rtmp_handshake.h"
#include "rtmp_log.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#define RTMP_HANDSHAKE_TOTAL_SIZE (RTMP_HANDSHAKE_VERSION_SIZE + RTMP_HANDSHAKE_PACKET_SIZE)

static int read_exact(int socket, uint8_t* buffer, size_t size) {
    size_t total_read = 0;
    
    while (total_read < size) {
        ssize_t bytes = read(socket, buffer + total_read, size - total_read);
        if (bytes <= 0) {
            LOG_ERROR("Failed to read from socket: %zd", bytes);
            return -1;
        }
        total_read += bytes;
    }
    
    return 0;
}

static int write_exact(int socket, const uint8_t* buffer, size_t size) {
    size_t total_written = 0;
    
    while (total_written < size) {
        ssize_t bytes = write(socket, buffer + total_written, size - total_written);
        if (bytes <= 0) {
            LOG_ERROR("Failed to write to socket: %zd", bytes);
            return -1;
        }
        total_written += bytes;
    }
    
    return 0;
}

static void generate_handshake_data(uint8_t* data) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    // First 4 bytes are timestamp
    uint32_t timestamp = tv.tv_sec;
    data[0] = (timestamp >> 24) & 0xFF;
    data[1] = (timestamp >> 16) & 0xFF;
    data[2] = (timestamp >> 8) & 0xFF;
    data[3] = timestamp & 0xFF;
    
    // Next 4 bytes are zeros for Flash player compatibility
    memset(data + 4, 0, 4);
    
    // Remaining bytes are random
    for (int i = 8; i < RTMP_HANDSHAKE_PACKET_SIZE; i++) {
        data[i] = rand() & 0xFF;
    }
}

int rtmp_handshake_server(int socket) {
    uint8_t version;
    uint8_t s1[RTMP_HANDSHAKE_PACKET_SIZE];
    uint8_t c1[RTMP_HANDSHAKE_PACKET_SIZE];
    uint8_t s2[RTMP_HANDSHAKE_PACKET_SIZE];
    
    // Read C0 (version)
    if (read_exact(socket, &version, 1) < 0) {
        LOG_ERROR("Failed to read C0");
        return -1;
    }
    
    if (version != RTMP_HANDSHAKE_VERSION_OLD) {
        LOG_ERROR("Unsupported RTMP version: %d", version);
        return -1;
    }
    
    LOG_DEBUG("RTMP handshake version: %d", version);
    
    // Read C1
    if (read_exact(socket, c1, RTMP_HANDSHAKE_PACKET_SIZE) < 0) {
        LOG_ERROR("Failed to read C1");
        return -1;
    }
    
    // Generate and send S0+S1
    version = RTMP_HANDSHAKE_VERSION_OLD;
    if (write_exact(socket, &version, 1) < 0) {
        LOG_ERROR("Failed to write S0");
        return -1;
    }
    
    generate_handshake_data(s1);
    if (write_exact(socket, s1, RTMP_HANDSHAKE_PACKET_SIZE) < 0) {
        LOG_ERROR("Failed to write S1");
        return -1;
    }
    
    // Generate and send S2 (echo of C1)
    memcpy(s2, c1, RTMP_HANDSHAKE_PACKET_SIZE);
    if (write_exact(socket, s2, RTMP_HANDSHAKE_PACKET_SIZE) < 0) {
        LOG_ERROR("Failed to write S2");
        return -1;
    }
    
    // Read C2
    uint8_t c2[RTMP_HANDSHAKE_PACKET_SIZE];
    if (read_exact(socket, c2, RTMP_HANDSHAKE_PACKET_SIZE) < 0) {
        LOG_ERROR("Failed to read C2");
        return -1;
    }
    
    // Verify C2 matches S1
    if (memcmp(c2, s1, RTMP_HANDSHAKE_PACKET_SIZE) != 0) {
        LOG_WARNING("C2 does not match S1, but continuing anyway");
    }
    
    LOG_INFO("RTMP handshake completed successfully");
    return 0;
}

int rtmp_handshake_client(int socket) {
    // Cliente não implementado pois não é necessário para o servidor
    return -1;
}

int rtmp_handshake_verify(const uint8_t* data, size_t len) {
    if (!data || len != RTMP_HANDSHAKE_PACKET_SIZE) {
        return -1;
    }
    
    // Verificar timestamp
    uint32_t timestamp = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    if (timestamp == 0) {
        return -1;
    }
    
    // Verificar zeros
    for (int i = 4; i < 8; i++) {
        if (data[i] != 0) {
            return -1;
        }
    }
    
    return 0;
}