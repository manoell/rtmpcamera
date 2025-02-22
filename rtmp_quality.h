#ifndef RTMP_QUALITY_H
#define RTMP_QUALITY_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>

// Forward declarations
typedef struct RTMPStream RTMPStream;
typedef struct RTMPPacket RTMPPacket;

// Initialize quality control for a stream
void rtmp_quality_init(RTMPStream *stream);

// Update quality metrics based on received packet
void rtmp_quality_update(RTMPStream *stream, const RTMPPacket *packet);

// Adjust stream quality based on current metrics
void rtmp_quality_adjust(RTMPStream *stream);

// Reset quality control to default values
void rtmp_quality_reset(RTMPStream *stream);

// Utility function to get time difference in milliseconds
static inline int64_t timeval_diff_ms(struct timeval *t1, struct timeval *t2) {
    return ((int64_t)(t1->tv_sec - t2->tv_sec) * 1000 +
            (int64_t)(t1->tv_usec - t2->tv_usec) / 1000);
}

#endif // RTMP_QUALITY_H