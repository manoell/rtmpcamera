#ifndef RTMP_PREVIEW_H
#define RTMP_PREVIEW_H

#import <UIKit/UIKit.h>
#import "rtmp_protocol.h"

@interface RTMPPreviewWindow : UIWindow

@property (nonatomic, assign) BOOL draggable;
@property (nonatomic, assign) BOOL showStats;
@property (nonatomic, assign) CGFloat previewScale;

// Main control
- (void)show;
- (void)hide;
- (void)updateWithStream:(RTMPStream *)stream;

// Position control
- (void)setPosition:(CGPoint)position;
- (void)setSize:(CGSize)size;

// Stats display
- (void)setStatsVisible:(BOOL)visible;
- (void)updateStats:(NSDictionary *)stats;

// Stream controls
- (void)enableStreamControls:(BOOL)enable;
- (void)setStreamQuality:(RTMPQualityLevel)quality;

@end

// Preview delegate protocol
@protocol RTMPPreviewDelegate <NSObject>
@optional
- (void)previewDidMove:(RTMPPreviewWindow *)preview;
- (void)previewDidResize:(RTMPPreviewWindow *)preview;
- (void)previewDidTap:(RTMPPreviewWindow *)preview;
- (void)preview:(RTMPPreviewWindow *)preview didChangeQuality:(RTMPQualityLevel)quality;
@end

#endif /* RTMP_PREVIEW_H */