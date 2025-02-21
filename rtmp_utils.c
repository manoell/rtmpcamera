#include "rtmp_utils.h"
#include "rtmp_core.h"
#include <errno.h>
#include <sys/select.h>
#include <sys/socket.h>

ssize_t read_with_timeout(int fd, void *buf, size_t count, int timeout_ms) {
    fd_set readfds;
    struct timeval tv;
    size_t total_read = 0;
    
    while (total_read < count) {
        FD_ZERO(&readfds);
        FD_SET(fd, &readfds);
        
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        
        int ret = select(fd + 1, &readfds, NULL, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (ret == 0) {
            rtmp_log(RTMP_LOG_ERROR, "Read timeout after %d ms", timeout_ms);
            return -1;
        }
        
        ssize_t bytes = recv(fd, (char*)buf + total_read, count - total_read, 0);
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -1;
        }
        if (bytes == 0) {
            return total_read > 0 ? total_read : 0;
        }
        
        total_read += bytes;
    }
    
    return total_read;
}

ssize_t write_with_timeout(int fd, const void *buf, size_t count, int timeout_ms) {
    fd_set writefds;
    struct timeval tv;
    size_t total_written = 0;
    
    while (total_written < count) {
        FD_ZERO(&writefds);
        FD_SET(fd, &writefds);
        
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        
        int ret = select(fd + 1, NULL, &writefds, NULL, &tv);
        if (ret < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (ret == 0) {
            rtmp_log(RTMP_LOG_ERROR, "Write timeout after %d ms", timeout_ms);
            return -1;
        }
        
        ssize_t bytes = send(fd, (char*)buf + total_written, count - total_written, 0);
        if (bytes < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            return -1;
        }
        
        total_written += bytes;
    }
    
    return total_written;
}