#import <Foundation/Foundation.h>
#import <CoreMedia/CoreMedia.h>

@interface RTMPPreviewController : NSObject

// Preview control
- (void)showPreview;
- (void)hidePreview;
- (void)toggleMinimize;

// Frame handling
- (void)updateFrame:(CMSampleBufferRef)sampleBuffer;

// Statistics and metrics
- (void)updateStreamMetrics:(NSDictionary *)metrics;

@end