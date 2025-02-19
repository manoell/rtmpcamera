#ifndef RTMP_PREVIEW_H
#define RTMP_PREVIEW_H

#ifdef __OBJC__
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>

@interface RTMPPreviewView : UIView

@property (nonatomic, strong) AVSampleBufferDisplayLayer *previewLayer;
@property (nonatomic, assign) BOOL isVisible;

+ (instancetype)sharedInstance;
- (void)showPreview;
- (void)hidePreview;
- (void)processVideoData:(uint8_t *)data length:(size_t)length timestamp:(uint32_t)timestamp;
- (void)processAudioData:(uint8_t *)data length:(size_t)length timestamp:(uint32_t)timestamp;
- (void)displayDecodedFrame:(CVImageBufferRef)imageBuffer withTimestamp:(CMTime)timestamp;

@end
#endif

// C interface for the preview system
#ifdef __cplusplus
extern "C" {
#endif

void rtmp_preview_init(void);
void rtmp_preview_show(void);
void rtmp_preview_hide(void);
void rtmp_preview_process_video(const uint8_t* data, size_t length, uint32_t timestamp);
void rtmp_preview_process_audio(const uint8_t* data, size_t length, uint32_t timestamp);

#ifdef __cplusplus
}
#endif

#endif // RTMP_PREVIEW_H