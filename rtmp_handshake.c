#include "rtmp_handshake.h"
#include "rtmp_log.h"
#include "rtmp_util.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#define RTMP_HANDSHAKE_PKT_SIZE (1 + RTMP_HANDSHAKE_PACKET_SIZE)
#define RTMP_HANDSHAKE_RAND_OFFSET 8

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
    // Zero o buffer
    memset(data, 0, RTMP_HANDSHAKE_PACKET_SIZE);
    
    // Timestamp
    uint32_t timestamp = get_timestamp();
    write_uint32(data, timestamp);
    
    // Versão do Flash (zeros)
    write_uint32(data + 4, 0);
    
    // Random data
    for (int i = RTMP_HANDSHAKE_RAND_OFFSET; i < RTMP_HANDSHAKE_PACKET_SIZE; i++) {
        data[i] = rand() % 256;
    }
}

int rtmp_handshake_server(int socket) {
    uint8_t version;
    uint8_t s0[1];
    uint8_t s1[RTMP_HANDSHAKE_PACKET_SIZE];
    uint8_t s2[RTMP_HANDSHAKE_PACKET_SIZE];
    uint8_t c1[RTMP_HANDSHAKE_PACKET_SIZE];
    uint8_t c2[RTMP_HANDSHAKE_PACKET_SIZE];
    
    // Ler C0 (versão)
    if (read_exact(socket, &version, 1) < 0) {
        LOG_ERROR("Failed to read C0");
        return -1;
    }
    
    // Verificar versão
    if (version != RTMP_HANDSHAKE_VERSION_OLD) {
        LOG_ERROR("Unsupported RTMP version: %d", version);
        return -1;
    }
    
    LOG_DEBUG("RTMP handshake version: %d", version);
    
    // Ler C1
    if (read_exact(socket, c1, RTMP_HANDSHAKE_PACKET_SIZE) < 0) {
        LOG_ERROR("Failed to read C1");
        return -1;
    }
    
    // Gerar e enviar S0+S1
    s0[0] = RTMP_HANDSHAKE_VERSION_OLD;
    if (write_exact(socket, s0, 1) < 0) {
        LOG_ERROR("Failed to write S0");
        return -1;
    }
    
    generate_handshake_data(s1);
    if (write_exact(socket, s1, RTMP_HANDSHAKE_PACKET_SIZE) < 0) {
        LOG_ERROR("Failed to write S1");
        return -1;
    }
    
    // Gerar e enviar S2 (eco de C1)
    memcpy(s2, c1, RTMP_HANDSHAKE_PACKET_SIZE);
    if (write_exact(socket, s2, RTMP_HANDSHAKE_PACKET_SIZE) < 0) {
        LOG_ERROR("Failed to write S2");
        return -1;
    }
    
    // Ler C2
    if (read_exact(socket, c2, RTMP_HANDSHAKE_PACKET_SIZE) < 0) {
        LOG_ERROR("Failed to read C2");
        return -1;
    }
    
    // Verificar C2 corresponde a S1
    if (memcmp(c2, s1, RTMP_HANDSHAKE_PACKET_SIZE) != 0) {
        LOG_WARNING("C2 does not match S1 - continuing anyway");
    }
    
    LOG_INFO("RTMP handshake completed successfully");
    return 0;
}

int rtmp_handshake_client(int socket) {
    // Não é necessário para o servidor
    (void)socket;
    return -1;
}

int rtmp_handshake_verify(const uint8_t* data, size_t len) {
    if (!data || len != RTMP_HANDSHAKE_PACKET_SIZE) {
        return -1;
    }
    
    // Verificar timestamp
    uint32_t timestamp = read_uint32(data);
    if (timestamp == 0) {
        return -1;
    }
    
    // Verificar zeros da versão do Flash
    uint32_t flash_version = read_uint32(data + 4);
    if (flash_version != 0) {
        return -1;
    }
    
    return 0;
}