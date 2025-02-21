#ifndef RTMP_UTILS_H
#define RTMP_UTILS_H

#include <sys/types.h>

// Funções de timeout
ssize_t read_with_timeout(int fd, void *buf, size_t count, int timeout_ms);
ssize_t write_with_timeout(int fd, const void *buf, size_t count, int timeout_ms);

#endif