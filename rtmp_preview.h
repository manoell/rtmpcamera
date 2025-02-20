// rtmp_preview.h
#ifndef RTMP_PREVIEW_H
#define RTMP_PREVIEW_H

#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import "rtmp_core.h"

@interface RTMPPreviewView : UIView

// Configuração
- (void)setupPreviewWithSize:(CGSize)size;
- (void)startPreview;
- (void)stopPreview;

// Controle de display
- (void)displayVideoFrame:(CMSampleBufferRef)sampleBuffer;
- (void)handlePan:(UIPanGestureRecognizer *)gesture;

// Status do preview
@property (nonatomic, readonly) BOOL isPreviewRunning;
@property (nonatomic, readonly) CGSize streamSize;
@property (nonatomic, assign) float streamFPS;

@end

#endif