#include "rtmp_quality.h"
#include "rtmp_core.h"
#include "rtmp_diagnostics.h"
#include <string.h>

// Constants for quality control
#define MIN_BITRATE 500000    // 500 Kbps
#define MAX_BITRATE 8000000   // 8 Mbps
#define TARGET_BUFFER_MS 500  // Target buffer size in milliseconds
#define MAX_FRAME_SKIP 2      // Maximum number of frames to skip

// Quality control structure
typedef struct {
    uint32_t current_bitrate;
    uint32_t target_bitrate;
    uint32_t buffer_size;
    uint32_t frame_count;
    uint32_t skip_count;
    double avg_frame_size;
    struct timeval last_adjustment;
    bool is_adjusting;
} RTMPQualityControl;

static RTMPQualityControl quality_control = {
    .current_bitrate = 2000000,  // Start at 2 Mbps
    .target_bitrate = 2000000,
    .buffer_size = 0,
    .frame_count = 0,
    .skip_count = 0,
    .avg_frame_size = 0,
    .is_adjusting = false
};

void rtmp_quality_init(RTMPStream *stream) {
    if (!stream) return;
    
    // Initialize quality settings
    stream->video_bitrate = quality_control.current_bitrate;
    stream->video_quality = 90; // Start with high quality
    
    gettimeofday(&quality_control.last_adjustment, NULL);
}

void rtmp_quality_update(RTMPStream *stream, const RTMPPacket *packet) {
    if (!stream || !packet) return;

    struct timeval now;
    gettimeofday(&now, NULL);

    // Update statistics
    quality_control.frame_count++;
    quality_control.buffer_size += packet->m_nBodySize;
    quality_control.avg_frame_size = 
        (quality_control.avg_frame_size * (quality_control.frame_count - 1) + packet->m_nBodySize) 
        / quality_control.frame_count;

    // Check if we need to adjust quality
    if (timeval_diff_ms(&now, &quality_control.last_adjustment) > 1000) {
        rtmp_quality_adjust(stream);
        quality_control.last_adjustment = now;
    }

    // Handle frame skipping if needed
    if (quality_control.buffer_size > (TARGET_BUFFER_MS * quality_control.current_bitrate / 8000)) {
        if (quality_control.skip_count < MAX_FRAME_SKIP) {
            quality_control.skip_count++;
            return;
        }
        quality_control.skip_count = 0;
    }

    // Reset buffer size periodically
    if (quality_control.frame_count % 30 == 0) {
        quality_control.buffer_size = 0;
    }
}

void rtmp_quality_adjust(RTMPStream *stream) {
    if (!stream || quality_control.is_adjusting) return;

    quality_control.is_adjusting = true;

    // Calculate network conditions
    double buffer_ratio = (double)quality_control.buffer_size / 
                         (TARGET_BUFFER_MS * quality_control.current_bitrate / 8000);

    // Adjust bitrate based on conditions
    if (buffer_ratio > 1.2) {
        // Buffer growing too large, reduce bitrate
        quality_control.target_bitrate = 
            (uint32_t)(quality_control.current_bitrate * 0.8);
    } else if (buffer_ratio < 0.8) {
        // Buffer too small, increase bitrate
        quality_control.target_bitrate = 
            (uint32_t)(quality_control.current_bitrate * 1.2);
    }

    // Clamp bitrate to acceptable range
    if (quality_control.target_bitrate < MIN_BITRATE)
        quality_control.target_bitrate = MIN_BITRATE;
    if (quality_control.target_bitrate > MAX_BITRATE)
        quality_control.target_bitrate = MAX_BITRATE;

    // Apply new bitrate if changed
    if (quality_control.target_bitrate != quality_control.current_bitrate) {
        quality_control.current_bitrate = quality_control.target_bitrate;
        stream->video_bitrate = quality_control.current_bitrate;
        
        // Adjust video quality based on bitrate
        if (quality_control.current_bitrate > 4000000)
            stream->video_quality = 90;
        else if (quality_control.current_bitrate > 2000000)
            stream->video_quality = 85;
        else
            stream->video_quality = 80;

        rtmp_diagnostic_log("Quality adjusted - Bitrate: %u, Quality: %d", 
                          quality_control.current_bitrate, 
                          stream->video_quality);
    }

    quality_control.is_adjusting = false;
}

void rtmp_quality_reset(RTMPStream *stream) {
    if (!stream) return;

    // Reset quality control structure
    quality_control.current_bitrate = 2000000;
    quality_control.target_bitrate = 2000000;
    quality_control.buffer_size = 0;
    quality_control.frame_count = 0;
    quality_control.skip_count = 0;
    quality_control.avg_frame_size = 0;
    quality_control.is_adjusting = false;

    // Reset stream quality settings
    stream->video_bitrate = quality_control.current_bitrate;
    stream->video_quality = 90;

    gettimeofday(&quality_control.last_adjustment, NULL);
}