#include "rtmp_handshake.h"
#include "rtmp_utils.h"
#include "rtmp_diagnostics.h"
#include <string.h>

#define RTMP_HANDSHAKE_SIZE 1536
#define RTMP_HANDSHAKE_VERSION 3
#define RTMP_DIGEST_LENGTH 32
#define RTMP_HANDSHAKE_TIMEOUT 5000 // 5 seconds

// Handshake patterns
static const uint8_t RTMP_CLIENT_PATTERN[] = {
    'G', 'e', 'n', 'u', 'i', 'n', 'e', ' ', 'A', 'd', 'o', 'b', 'e', ' ',
    'F', 'l', 'a', 's', 'h', ' ', 'P', 'l', 'a', 'y', 'e', 'r', ' ',
    '0', '0', '1'
};

static const uint8_t RTMP_SERVER_PATTERN[] = {
    'G', 'e', 'n', 'u', 'i', 'n', 'e', ' ', 'A', 'd', 'o', 'b', 'e', ' ',
    'F', 'l', 'a', 's', 'h', ' ', 'M', 'e', 'd', 'i', 'a', ' ',
    'S', 'e', 'r', 'v', 'e', 'r'
};

// HMAC keys
static const uint8_t RTMP_CLIENT_KEY[] = {
    'G', 'e', 'n', 'u', 'i', 'n', 'e', ' ', 'A', 'd', 'o', 'b', 'e', ' ',
    'F', 'l', 'a', 's', 'h', ' ', 'P', 'l', 'a', 'y', 'e', 'r'
};

static const uint8_t RTMP_SERVER_KEY[] = {
    'G', 'e', 'n', 'u', 'i', 'n', 'e', ' ', 'A', 'd', 'o', 'b', 'e', ' ',
    'F', 'l', 'a', 's', 'h', ' ', 'M', 'e', 'd', 'i', 'a', ' ',
    'S', 'e', 'r', 'v', 'e', 'r', ' ',
    '0', '0', '1'
};

// Calculate positions for the digest
static void rtmp_handshake_get_digest_offset(const uint8_t *buf, size_t size,
                                           int *offset1, int *offset2) {
    // Hash offset calculation based on first 4 bytes
    uint32_t offset = ((uint32_t)buf[0] + buf[1] + buf[2] + buf[3]) % 728 + 12;
    
    *offset1 = offset;
    *offset2 = size - RTMP_DIGEST_LENGTH - offset;
}

// Validate client digest
static int rtmp_handshake_validate_client_digest(const uint8_t *handshake,
                                               const uint8_t *key,
                                               size_t key_size) {
    uint8_t digest[RTMP_DIGEST_LENGTH];
    int offset1, offset2;
    
    rtmp_handshake_get_digest_offset(handshake, RTMP_HANDSHAKE_SIZE, &offset1, &offset2);
    
    // Calculate expected digest
    uint8_t *temp = rtmp_utils_malloc(RTMP_HANDSHAKE_SIZE);
    if (!temp) return -1;
    
    memcpy(temp, handshake, RTMP_HANDSHAKE_SIZE);
    memset(temp + offset1, 0, RTMP_DIGEST_LENGTH);
    
    rtmp_utils_hmac_sha256(key, key_size, temp, RTMP_HANDSHAKE_SIZE, digest);
    rtmp_utils_free(temp);
    
    // Compare with received digest
    return memcmp(digest, handshake + offset1, RTMP_DIGEST_LENGTH);
}

// Generate server digest
static void rtmp_handshake_generate_server_digest(const uint8_t *handshake,
                                                uint8_t *output) {
    int offset1, offset2;
    rtmp_handshake_get_digest_offset(handshake, RTMP_HANDSHAKE_SIZE, &offset1, &offset2);
    
    // Calculate server digest
    uint8_t *temp = rtmp_utils_malloc(RTMP_HANDSHAKE_SIZE);
    if (!temp) return;
    
    memcpy(temp, handshake, RTMP_HANDSHAKE_SIZE);
    memset(temp + offset2, 0, RTMP_DIGEST_LENGTH);
    
    rtmp_utils_hmac_sha256(RTMP_SERVER_KEY, sizeof(RTMP_SERVER_KEY),
                         temp, RTMP_HANDSHAKE_SIZE, output);
    rtmp_utils_free(temp);
}

// Perform server handshake
int rtmp_handshake_server(rtmp_connection_t *conn) {
    if (!conn) return RTMP_ERROR_INVALID_PARAM;
    
    uint8_t c0, s0 = RTMP_HANDSHAKE_VERSION;
    uint8_t c1[RTMP_HANDSHAKE_SIZE];
    uint8_t s1[RTMP_HANDSHAKE_SIZE];
    uint8_t c2[RTMP_HANDSHAKE_SIZE];
    uint8_t s2[RTMP_HANDSHAKE_SIZE];
    
    rtmp_log_debug("Starting RTMP handshake");
    
    // Receive C0
    if (rtmp_utils_receive(conn->socket, &c0, 1, RTMP_HANDSHAKE_TIMEOUT) != 1) {
        rtmp_log_error("Failed to receive C0");
        return RTMP_ERROR_HANDSHAKE_C0;
    }
    
    // Validate version
    if (c0 != RTMP_HANDSHAKE_VERSION) {
        rtmp_log_error("Invalid RTMP version: %d", c0);
        return RTMP_ERROR_VERSION;
    }
    
    // Receive C1
    if (rtmp_utils_receive(conn->socket, c1, RTMP_HANDSHAKE_SIZE, RTMP_HANDSHAKE_TIMEOUT) != RTMP_HANDSHAKE_SIZE) {
        rtmp_log_error("Failed to receive C1");
        return RTMP_ERROR_HANDSHAKE_C1;
    }
    
    // Validate client digest
    if (rtmp_handshake_validate_client_digest(c1, RTMP_CLIENT_KEY, sizeof(RTMP_CLIENT_KEY)) != 0) {
        rtmp_log_warning("Invalid client digest, continuing anyway");
    }
    
    // Generate S1
    rtmp_utils_random_bytes(s1, RTMP_HANDSHAKE_SIZE);
    
    // Set timestamp
    uint32_t timestamp = rtmp_utils_get_time_ms();
    memcpy(s1, &timestamp, 4);
    memset(s1 + 4, 0, 4);  // Zero out the time2 field
    
    // Generate and embed server digest
    uint8_t server_digest[RTMP_DIGEST_LENGTH];
    rtmp_handshake_generate_server_digest(s1, server_digest);
    
    int offset1, offset2;
    rtmp_handshake_get_digest_offset(s1, RTMP_HANDSHAKE_SIZE, &offset1, &offset2);
    memcpy(s1 + offset2, server_digest, RTMP_DIGEST_LENGTH);
    
    // Send S0+S1
    if (rtmp_utils_send(conn->socket, &s0, 1, RTMP_HANDSHAKE_TIMEOUT) != 1 ||
        rtmp_utils_send(conn->socket, s1, RTMP_HANDSHAKE_SIZE, RTMP_HANDSHAKE_TIMEOUT) != RTMP_HANDSHAKE_SIZE) {
        rtmp_log_error("Failed to send S0+S1");
        return RTMP_ERROR_HANDSHAKE_S0S1;
    }
    
    // Generate S2 (copy of C1)
    memcpy(s2, c1, RTMP_HANDSHAKE_SIZE);
    
    // Send S2
    if (rtmp_utils_send(conn->socket, s2, RTMP_HANDSHAKE_SIZE, RTMP_HANDSHAKE_TIMEOUT) != RTMP_HANDSHAKE_SIZE) {
        rtmp_log_error("Failed to send S2");
        return RTMP_ERROR_HANDSHAKE_S2;
    }
    
    // Receive C2
    if (rtmp_utils_receive(conn->socket, c2, RTMP_HANDSHAKE_SIZE, RTMP_HANDSHAKE_TIMEOUT) != RTMP_HANDSHAKE_SIZE) {
        rtmp_log_error("Failed to receive C2");
        return RTMP_ERROR_HANDSHAKE_C2;
    }
    
    // Validate C2 (should be copy of S1)
    if (memcmp(c2, s1, RTMP_HANDSHAKE_SIZE) != 0) {
        rtmp_log_warning("C2 does not match S1, continuing anyway");
    }
    
    rtmp_log_debug("RTMP handshake completed successfully");
    return RTMP_SUCCESS;
}

// Perform client handshake
int rtmp_handshake_client(rtmp_connection_t *conn) {
    if (!conn) return RTMP_ERROR_INVALID_PARAM;
    
    uint8_t c0 = RTMP_HANDSHAKE_VERSION, s0;
    uint8_t c1[RTMP_HANDSHAKE_SIZE];
    uint8_t s1[RTMP_HANDSHAKE_SIZE];
    uint8_t c2[RTMP_HANDSHAKE_SIZE];
    uint8_t s2[RTMP_HANDSHAKE_SIZE];
    
    rtmp_log_debug("Starting RTMP client handshake");
    
    // Generate C1
    rtmp_utils_random_bytes(c1, RTMP_HANDSHAKE_SIZE);
    
    // Set timestamp
    uint32_t timestamp = rtmp_utils_get_time_ms();
    memcpy(c1, &timestamp, 4);
    memset(c1 + 4, 0, 4);  // Zero out the time2 field
    
    // Generate and embed client digest
    uint8_t client_digest[RTMP_DIGEST_LENGTH];
    rtmp_utils_hmac_sha256(RTMP_CLIENT_KEY, sizeof(RTMP_CLIENT_KEY),
                         c1, RTMP_HANDSHAKE_SIZE, client_digest);
    
    int offset1, offset2;
    rtmp_handshake_get_digest_offset(c1, RTMP_HANDSHAKE_SIZE, &offset1, &offset2);
    memcpy(c1 + offset1, client_digest, RTMP_DIGEST_LENGTH);
    
    // Send C0+C1
    if (rtmp_utils_send(conn->socket, &c0, 1, RTMP_HANDSHAKE_TIMEOUT) != 1 ||
        rtmp_utils_send(conn->socket, c1, RTMP_HANDSHAKE_SIZE, RTMP_HANDSHAKE_TIMEOUT) != RTMP_HANDSHAKE_SIZE) {
        rtmp_log_error("Failed to send C0+C1");
        return RTMP_ERROR_HANDSHAKE_C0C1;
    }
    
    // Receive S0
    if (rtmp_utils_receive(conn->socket, &s0, 1, RTMP_HANDSHAKE_TIMEOUT) != 1) {
        rtmp_log_error("Failed to receive S0");
        return RTMP_ERROR_HANDSHAKE_S0;
    }
    
    // Validate version
    if (s0 != RTMP_HANDSHAKE_VERSION) {
        rtmp_log_error("Invalid RTMP version from server: %d", s0);
        return RTMP_ERROR_VERSION;
    }
    
    // Receive S1
    if (rtmp_utils_receive(conn->socket, s1, RTMP_HANDSHAKE_SIZE, RTMP_HANDSHAKE_TIMEOUT) != RTMP_HANDSHAKE_SIZE) {
        rtmp_log_error("Failed to receive S1");
        return RTMP_ERROR_HANDSHAKE_S1;
    }
    
    // Generate C2 (copy of S1)
    memcpy(c2, s1, RTMP_HANDSHAKE_SIZE);
    
    // Send C2
    if (rtmp_utils_send(conn->socket, c2, RTMP_HANDSHAKE_SIZE, RTMP_HANDSHAKE_TIMEOUT) != RTMP_HANDSHAKE_SIZE) {
        rtmp_log_error("Failed to send C2");
        return RTMP_ERROR_HANDSHAKE_C2;
    }
    
    // Receive S2
    if (rtmp_utils_receive(conn->socket, s2, RTMP_HANDSHAKE_SIZE, RTMP_HANDSHAKE_TIMEOUT) != RTMP_HANDSHAKE_SIZE) {
        rtmp_log_error("Failed to receive S2");
        return RTMP_ERROR_HANDSHAKE_S2;
    }
    
    // Validate S2 (should be copy of C1)
    if (memcmp(s2, c1, RTMP_HANDSHAKE_SIZE) != 0) {
        rtmp_log_warning("S2 does not match C1, continuing anyway");
    }
    
    rtmp_log_debug("RTMP client handshake completed successfully");
    return RTMP_SUCCESS;
}