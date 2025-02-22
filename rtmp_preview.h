#ifndef RTMP_PREVIEW_H
#define RTMP_PREVIEW_H

#import <CoreVideo/CoreVideo.h>

// Forward declaration
typedef struct RTMPStream RTMPStream;

// Show preview window with stream statistics
void rtmp_preview_show(RTMPStream *stream);

// Hide preview window
void rtmp_preview_hide(void);

// Update preview with new frame
void rtmp_preview_update_frame(CVImageBufferRef imageBuffer);

#endif // RTMP_PREVIEW_H