#include "rtmp_handshake.h"
#include "rtmp_utils.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

// Local types
typedef struct {
    uint32_t time;
    uint32_t version;
    uint8_t random[RTMP_HANDSHAKE_SIZE - 8];
} rtmp_handshake_packet_t;

int rtmp_handshake_server(rtmp_session_t *session) {
    if (!session) return -1;
    
    // Read C0 (version byte)
    if (rtmp_handshake_read_version(session) < 0) {
        return -1;
    }
    
    // Generate and send S0+S1
    if (rtmp_handshake_write_s0s1(session) < 0) {
        return -1;
    }
    
    // Read C1
    uint8_t c1[RTMP_HANDSHAKE_SIZE];
    if (rtmp_handshake_read_c1(session) < 0) {
        return -1;
    }
    
    // Generate and send S2
    if (rtmp_handshake_write_s2(session) < 0) {
        return -1;
    }
    
    // Read C2
    if (rtmp_handshake_read_c2(session) < 0) {
        return -1;
    }
    
    return 0;
}

int rtmp_handshake_read_version(rtmp_session_t *session) {
    uint8_t version;
    ssize_t bytes_read = recv(session->socket_fd, &version, 1, 0);
    
    if (bytes_read != 1) {
        return -1;
    }
    
    if (version != RTMP_HANDSHAKE_VERSION) {
        return -1;
    }
    
    return 0;
}

int rtmp_handshake_write_s0s1(rtmp_session_t *session) {
    uint8_t packet[RTMP_HANDSHAKE_PACKET_SIZE];
    rtmp_handshake_packet_t *s1 = (rtmp_handshake_packet_t *)(packet + 1);
    
    // Write S0 (version)
    packet[0] = RTMP_HANDSHAKE_VERSION;
    
    // Generate S1
    s1->time = rtmp_handshake_get_time();
    s1->version = 0;
    if (rtmp_handshake_generate_random(s1->random, sizeof(s1->random)) < 0) {
        return -1;
    }
    
    // Send S0+S1
    if (rtmp_session_send_data(session, packet, RTMP_HANDSHAKE_PACKET_SIZE) < 0) {
        return -1;
    }
    
    return 0;
}

int rtmp_handshake_read_c1(rtmp_session_t *session) {
    uint8_t packet[RTMP_HANDSHAKE_SIZE];
    
    // Read full C1 packet
    size_t total_read = 0;
    while (total_read < RTMP_HANDSHAKE_SIZE) {
        ssize_t bytes_read = recv(session->socket_fd, 
                                packet + total_read, 
                                RTMP_HANDSHAKE_SIZE - total_read, 
                                0);
        
        if (bytes_read <= 0) {
            return -1;
        }
        
        total_read += bytes_read;
    }
    
    return 0;
}

int rtmp_handshake_write_s2(rtmp_session_t *session) {
    uint8_t packet[RTMP_HANDSHAKE_SIZE];
    rtmp_handshake_packet_t *s2 = (rtmp_handshake_packet_t *)packet;
    
    // Generate S2
    s2->time = rtmp_handshake_get_time();
    s2->version = 0;
    if (rtmp_handshake_generate_random(s2->random, sizeof(s2->random)) < 0) {
        return -1;
    }
    
    // Send S2
    if (rtmp_session_send_data(session, packet, RTMP_HANDSHAKE_SIZE) < 0) {
        return -1;
    }
    
    return 0;
}

int rtmp_handshake_read_c2(rtmp_session_t *session) {
    uint8_t packet[RTMP_HANDSHAKE_SIZE];
    
    // Read full C2 packet
    size_t total_read = 0;
    while (total_read < RTMP_HANDSHAKE_SIZE) {
        ssize_t bytes_read = recv(session->socket_fd, 
                                packet + total_read, 
                                RTMP_HANDSHAKE_SIZE - total_read, 
                                0);
        
        if (bytes_read <= 0) {
            return -1;
        }
        
        total_read += bytes_read;
    }
    
    return 0;
}

int rtmp_handshake_generate_random(uint8_t *buffer, size_t size) {
    if (!buffer || !size) return -1;
    
    for (size_t i = 0; i < size; i++) {
        buffer[i] = rand() & 0xFF;
    }
    
    return 0;
}

uint32_t rtmp_handshake_get_time(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

int rtmp_handshake_verify_digest(const uint8_t *buffer, size_t size) {
    // Simplified digest verification
    // In a production environment, you would want to implement proper digest verification
    return 0;
}