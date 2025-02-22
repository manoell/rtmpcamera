#include "rtmp_utils.h"
#include "rtmp_diagnostics.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define RTMP_UTILS_RANDOM_BUFFER_SIZE 4096
#define RTMP_UTILS_MAX_TEMP_BUFFER 65536

// Thread-safe random number generator
static pthread_mutex_t random_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint8_t random_buffer[RTMP_UTILS_RANDOM_BUFFER_SIZE];
static size_t random_buffer_index = RTMP_UTILS_RANDOM_BUFFER_SIZE;

// Initialize randomization
void rtmp_utils_init_random(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    srand(tv.tv_sec ^ tv.tv_usec);
    
    pthread_mutex_lock(&random_mutex);
    for (size_t i = 0; i < RTMP_UTILS_RANDOM_BUFFER_SIZE; i++) {
        random_buffer[i] = (uint8_t)rand();
    }
    random_buffer_index = 0;
    pthread_mutex_unlock(&random_mutex);
}

// Get random bytes
void rtmp_utils_random_bytes(uint8_t *buffer, size_t size) {
    pthread_mutex_lock(&random_mutex);
    
    while (size > 0) {
        if (random_buffer_index >= RTMP_UTILS_RANDOM_BUFFER_SIZE) {
            for (size_t i = 0; i < RTMP_UTILS_RANDOM_BUFFER_SIZE; i++) {
                random_buffer[i] = (uint8_t)rand();
            }
            random_buffer_index = 0;
        }
        
        size_t copy_size = RTMP_UTILS_RANDOM_BUFFER_SIZE - random_buffer_index;
        if (copy_size > size) copy_size = size;
        
        memcpy(buffer, &random_buffer[random_buffer_index], copy_size);
        random_buffer_index += copy_size;
        buffer += copy_size;
        size -= copy_size;
    }
    
    pthread_mutex_unlock(&random_mutex);
}

// Get current timestamp in milliseconds
uint64_t rtmp_utils_get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}

// Sleep for specified milliseconds
void rtmp_utils_sleep_ms(uint32_t milliseconds) {
    usleep(milliseconds * 1000);
}

// Create server socket
int rtmp_utils_create_server_socket(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        rtmp_log_error("Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        rtmp_log_warning("Failed to set SO_REUSEADDR: %s", strerror(errno));
    }
    
    // Set TCP_NODELAY
    if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        rtmp_log_warning("Failed to set TCP_NODELAY: %s", strerror(errno));
    }
    
    // Set non-blocking
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }
    
    // Bind socket
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        rtmp_log_error("Failed to bind socket: %s", strerror(errno));
        close(sock);
        return -1;
    }
    
    // Listen
    if (listen(sock, SOMAXCONN) < 0) {
        rtmp_log_error("Failed to listen on socket: %s", strerror(errno));
        close(sock);
        return -1;
    }
    
    return sock;
}

// Accept client connection
int rtmp_utils_accept_connection(int server_socket) {
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    
    int client_socket = accept(server_socket, (struct sockaddr*)&addr, &addr_len);
    if (client_socket < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            rtmp_log_error("Failed to accept connection: %s", strerror(errno));
        }
        return -1;
    }
    
    // Set TCP_NODELAY
    int opt = 1;
    if (setsockopt(client_socket, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) < 0) {
        rtmp_log_warning("Failed to set TCP_NODELAY on client socket: %s", strerror(errno));
    }
    
    // Set non-blocking
    int flags = fcntl(client_socket, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);
    }
    
    rtmp_log_info("Accepted connection from %s:%d", 
                  inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    
    return client_socket;
}

// Send data with timeout
int rtmp_utils_send(int socket, const void *data, size_t size, int timeout_ms) {
    const uint8_t *ptr = (const uint8_t*)data;
    size_t remaining = size;
    uint64_t start_time = rtmp_utils_get_time_ms();
    
    while (remaining > 0) {
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(socket, &write_fds);
        
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        
        int result = select(socket + 1, NULL, &write_fds, NULL, &tv);
        if (result < 0) {
            if (errno == EINTR) continue;
            rtmp_log_error("Select failed: %s", strerror(errno));
            return -1;
        }
        if (result == 0) {
            rtmp_log_warning("Send timeout");
            return -1;
        }
        
        ssize_t sent = send(socket, ptr, remaining, 0);
        if (sent < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            rtmp_log_error("Send failed: %s", strerror(errno));
            return -1;
        }
        
        ptr += sent;
        remaining -= sent;
        
        // Check total timeout
        if (rtmp_utils_get_time_ms() - start_time > (uint64_t)timeout_ms) {
            rtmp_log_warning("Send total timeout");
            return -1;
        }
    }
    
    return size;
}

// Receive data with timeout
int rtmp_utils_receive(int socket, void *buffer, size_t size, int timeout_ms) {
    uint8_t *ptr = (uint8_t*)buffer;
    size_t remaining = size;
    uint64_t start_time = rtmp_utils_get_time_ms();
    
    while (remaining > 0) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(socket, &read_fds);
        
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        
        int result = select(socket + 1, &read_fds, NULL, NULL, &tv);
        if (result < 0) {
            if (errno == EINTR) continue;
            rtmp_log_error("Select failed: %s", strerror(errno));
            return -1;
        }
        if (result == 0) {
            rtmp_log_warning("Receive timeout");
            return -1;
        }
        
        ssize_t received = recv(socket, ptr, remaining, 0);
        if (received < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            rtmp_log_error("Receive failed: %s", strerror(errno));
            return -1;
        }
        if (received == 0) {
            rtmp_log_info("Connection closed by peer");
            return -1;
        }
        
        ptr += received;
        remaining -= received;
        
        // Check total timeout
        if (rtmp_utils_get_time_ms() - start_time > (uint64_t)timeout_ms) {
            rtmp_log_warning("Receive total timeout");
            return -1;
        }
    }
    
    return size;
}

// Calculate HMAC-SHA256
void rtmp_utils_hmac_sha256(const uint8_t *key, size_t key_size,
                          const uint8_t *data, size_t data_size,
                          uint8_t *output) {
    uint8_t k_ipad[64];
    uint8_t k_opad[64];
    uint8_t hash[32];
    
    // Prepare key
    memset(k_ipad, 0x36, sizeof(k_ipad));
    memset(k_opad, 0x5c, sizeof(k_opad));
    
    if (key_size > 64) {
        // Hash long keys
        SHA256_CTX ctx;
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, key, key_size);
        SHA256_Final(hash, &ctx);
        key = hash;
        key_size = 32;
    }
    
    for (size_t i = 0; i < key_size; i++) {
        k_ipad[i] ^= key[i];
        k_opad[i] ^= key[i];
    }
    
    // Inner hash
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, k_ipad, 64);
    SHA256_Update(&ctx, data, data_size);
    SHA256_Final(hash, &ctx);
    
    // Outer hash
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, k_opad, 64);
    SHA256_Update(&ctx, hash, 32);
    SHA256_Final(output, &ctx);
}

// Compress data using DEFLATE
int rtmp_utils_compress(const uint8_t *input, size_t input_size,
                      uint8_t *output, size_t *output_size) {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    
    if (deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
        rtmp_log_error("Failed to initialize deflate");
        return -1;
    }
    
    strm.next_in = (uint8_t*)input;
    strm.avail_in = input_size;
    strm.next_out = output;
    strm.avail_out = *output_size;
    
    int ret = deflate(&strm, Z_FINISH);
    deflateEnd(&strm);
    
    if (ret != Z_STREAM_END) {
        rtmp_log_error("Failed to compress data");
        return -1;
    }
    
    *output_size = strm.total_out;
    return 0;
}

// Decompress data using DEFLATE
int rtmp_utils_decompress(const uint8_t *input, size_t input_size,
                        uint8_t *output, size_t *output_size) {
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    
    if (inflateInit(&strm) != Z_OK) {
        rtmp_log_error("Failed to initialize inflate");
        return -1;
    }
    
    strm.next_in = (uint8_t*)input;
    strm.avail_in = input_size;
    strm.next_out = output;
    strm.avail_out = *output_size;
    
    int ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);
    
    if (ret != Z_STREAM_END) {
        rtmp_log_error("Failed to decompress data");
        return -1;
    }
    
    *output_size = strm.total_out;
    return 0;
}

// Network byte order conversion
uint16_t rtmp_utils_swap16(uint16_t value) {
    return (value >> 8) | (value << 8);
}

uint32_t rtmp_utils_swap32(uint32_t value) {
    return ((value & 0xFF000000) >> 24) |
           ((value & 0x00FF0000) >> 8) |
           ((value & 0x0000FF00) << 8) |
           ((value & 0x000000FF) << 24);
}

uint64_t rtmp_utils_swap64(uint64_t value) {
    return ((value & 0xFF00000000000000ULL) >> 56) |
           ((value & 0x00FF000000000000ULL) >> 40) |
           ((value & 0x0000FF0000000000ULL) >> 24) |
           ((value & 0x000000FF00000000ULL) >> 8) |
           ((value & 0x00000000FF000000ULL) << 8) |
           ((value & 0x0000000000FF0000ULL) << 24) |
           ((value & 0x000000000000FF00ULL) << 40) |
           ((value & 0x00000000000000FFULL) << 56);
}

// URL encoding/decoding
char* rtmp_utils_url_encode(const char *str) {
    if (!str) return NULL;
    
    const char *hex = "0123456789ABCDEF";
    char *result = malloc(strlen(str) * 3 + 1);
    char *ptr = result;
    
    while (*str) {
        if (isalnum(*str) || *str == '-' || *str == '_' || *str == '.' || *str == '~') {
            *ptr++ = *str;
        } else {
            *ptr++ = '%';
            *ptr++ = hex[(*str >> 4) & 0xF];
            *ptr++ = hex[*str & 0xF];
        }
        str++;
    }
    *ptr = '\0';
    
    return result;
}

char* rtmp_utils_url_decode(const char *str) {
    if (!str) return NULL;
    
    char *result = malloc(strlen(str) + 1);
    char *ptr = result;
    
    while (*str) {
        if (*str == '%') {
            if (str[1] && str[2]) {
                char hex[3] = { str[1], str[2], 0 };
                *ptr++ = (char)strtol(hex, NULL, 16);
                str += 3;
            } else {
                str++;
            }
        } else {
            *ptr++ = *str++;
        }
    }
    *ptr = '\0';
    
    return result;
}

// Base64 encoding/decoding
static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* rtmp_utils_base64_encode(const uint8_t *data, size_t size) {
    if (!data || size == 0) return NULL;
    
    size_t output_size = ((size + 2) / 3) * 4 + 1;
    char *result = malloc(output_size);
    char *ptr = result;
    
    while (size >= 3) {
        *ptr++ = base64_chars[data[0] >> 2];
        *ptr++ = base64_chars[((data[0] & 0x03) << 4) | (data[1] >> 4)];
        *ptr++ = base64_chars[((data[1] & 0x0F) << 2) | (data[2] >> 6)];
        *ptr++ = base64_chars[data[2] & 0x3F];
        
        data += 3;
        size -= 3;
    }
    
    if (size > 0) {
        *ptr++ = base64_chars[data[0] >> 2];
        if (size == 1) {
            *ptr++ = base64_chars[(data[0] & 0x03) << 4];
            *ptr++ = '=';
        } else {
            *ptr++ = base64_chars[((data[0] & 0x03) << 4) | (data[1] >> 4)];
            *ptr++ = base64_chars[(data[1] & 0x0F) << 2];
        }
        *ptr++ = '=';
    }
    
    *ptr = '\0';
    return result;
}

// Memory management
void* rtmp_utils_malloc(size_t size) {
    void *ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void* rtmp_utils_realloc(void *ptr, size_t new_size) {
    return realloc(ptr, new_size);
}

void rtmp_utils_free(void *ptr) {
    free(ptr);
}

// Buffer management
rtmp_buffer_t* rtmp_utils_buffer_create(size_t initial_size) {
    rtmp_buffer_t *buffer = rtmp_utils_malloc(sizeof(rtmp_buffer_t));
    if (!buffer) return NULL;
    
    buffer->data = rtmp_utils_malloc(initial_size);
    if (!buffer->data) {
        rtmp_utils_free(buffer);
        return NULL;
    }
    
    buffer->size = 0;
    buffer->capacity = initial_size;
    return buffer;
}

int rtmp_utils_buffer_append(rtmp_buffer_t *buffer, const void *data, size_t size) {
    if (!buffer || !data || size == 0) return -1;
    
    if (buffer->size + size > buffer->capacity) {
        size_t new_capacity = buffer->capacity * 2;
        if (new_capacity < buffer->size + size) {
            new_capacity = buffer->size + size;
        }
        
        uint8_t *new_data = rtmp_utils_realloc(buffer->data, new_capacity);
        if (!new_data) return -1;
        
        buffer->data = new_data;
        buffer->capacity = new_capacity;
    }
    
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return 0;
}

void rtmp_utils_buffer_clear(rtmp_buffer_t *buffer) {
    if (buffer) {
        buffer->size = 0;
    }
}

void rtmp_utils_buffer_destroy(rtmp_buffer_t *buffer) {
    if (buffer) {
        rtmp_utils_free(buffer->data);
        rtmp_utils_free(buffer);
    }
}

// String utilities
char* rtmp_utils_strdup(const char *str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char *result = rtmp_utils_malloc(len);
    if (result) {
        memcpy(result, str, len);
    }
    return result;
}

int rtmp_utils_strcasecmp(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int c1 = tolower(*s1);
        int c2 = tolower(*s2);
        if (c1 != c2) {
            return c1 - c2;
        }
        s1++;
        s2++;
    }
    return tolower(*s1) - tolower(*s2);
}

// Socket utilities
int rtmp_utils_set_socket_nonblocking(int socket) {
    int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(socket, F_SETFL, flags | O_NONBLOCK);
}

int rtmp_utils_set_socket_nodelay(int socket) {
    int flag = 1;
    return setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
}

int rtmp_utils_set_socket_keepalive(int socket) {
    int flag = 1;
    return setsockopt(socket, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));
}

// Cleanup function
void rtmp_utils_cleanup(void) {
    pthread_mutex_destroy(&random_mutex);
}