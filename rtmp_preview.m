#import "rtmp_preview.h"
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>

@interface RTMPPreviewView : UIView

@property (nonatomic, strong) UIImageView *videoPreview;
@property (nonatomic, strong) UILabel *statsLabel;
@property (nonatomic, strong) CADisplayLink *displayLink;
@property (nonatomic, assign) RTMPStream *stream;
@property (nonatomic, strong) UIButton *closeButton;
@property (nonatomic, strong) UIPanGestureRecognizer *panGesture;

@end

@implementation RTMPPreviewView

- (instancetype)initWithStream:(RTMPStream *)stream frame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _stream = stream;
        [self setupUI];
        [self setupGestures];
        [self startDisplayLink];
    }
    return self;
}

- (void)setupUI {
    self.backgroundColor = [UIColor blackColor];
    self.layer.cornerRadius = 10;
    self.layer.masksToBounds = true;
    self.alpha = 0.9;
    
    // Video preview
    _videoPreview = [[UIImageView alloc] initWithFrame:self.bounds];
    _videoPreview.contentMode = UIViewContentModeScaleAspectFit;
    [self addSubview:_videoPreview];
    
    // Stats label
    _statsLabel = [[UILabel alloc] initWithFrame:CGRectMake(5, 5, self.bounds.size.width - 10, 60)];
    _statsLabel.numberOfLines = 3;
    _statsLabel.font = [UIFont monospacedSystemFontOfSize:10 weight:UIFontWeightRegular];
    _statsLabel.textColor = [UIColor whiteColor];
    _statsLabel.backgroundColor = [UIColor colorWithWhite:0 alpha:0.5];
    _statsLabel.layer.cornerRadius = 5;
    _statsLabel.layer.masksToBounds = true;
    [self addSubview:_statsLabel];
    
    // Close button
    _closeButton = [UIButton buttonWithType:UIButtonTypeCustom];
    _closeButton.frame = CGRectMake(self.bounds.size.width - 30, 5, 25, 25);
    [_closeButton setTitle:@"×" forState:UIControlStateNormal];
    [_closeButton setTitleColor:[UIColor whiteColor] forState:UIControlStateNormal];
    _closeButton.backgroundColor = [UIColor colorWithRed:1 green:0 blue:0 alpha:0.7];
    _closeButton.layer.cornerRadius = 12.5;
    [_closeButton addTarget:self action:@selector(closeButtonTapped) forControlEvents:UIControlEventTouchUpInside];
    [self addSubview:_closeButton];
}

- (void)setupGestures {
    _panGesture = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePan:)];
    [self addGestureRecognizer:_panGesture];
}

- (void)startDisplayLink {
    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(updateStats)];
    [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)updateStats {
    if (!_stream) return;
    
    NSString *stats = [NSString stringWithFormat:
        @"FPS: %d | Bitrate: %.1f Mbps\n"
        @"Buffer: %d ms | Quality: %d%%\n"
        @"Resolution: %dx%d",
        _stream->stats.current_fps,
        (float)_stream->stats.current_bitrate / 1000000.0,
        _stream->stats.buffer_ms,
        _stream->stats.quality_percent,
        _stream->video_width,
        _stream->video_height
    ];
    
    dispatch_async(dispatch_get_main_queue(), ^{
        self.statsLabel.text = stats;
    });
}

- (void)updatePreviewImage:(CVImageBufferRef)imageBuffer {
    if (!imageBuffer) return;
    
    CIImage *ciImage = [CIImage imageWithCVPixelBuffer:imageBuffer];
    CIContext *context = [CIContext contextWithOptions:nil];
    CGImageRef cgImage = [context createCGImage:ciImage fromRect:ciImage.extent];
    
    dispatch_async(dispatch_get_main_queue(), ^{
        self.videoPreview.image = [UIImage imageWithCGImage:cgImage];
        CGImageRelease(cgImage);
    });
}

- (void)handlePan:(UIPanGestureRecognizer *)gesture {
    CGPoint translation = [gesture translationInView:self.superview];
    
    if (gesture.state == UIGestureRecognizerStateChanged) {
        self.center = CGPointMake(self.center.x + translation.x,
                                self.center.y + translation.y);
        [gesture setTranslation:CGPointZero inView:self.superview];
    }
    
    if (gesture.state == UIGestureRecognizerStateEnded) {
        // Keep window within screen bounds
        CGRect screenBounds = [UIScreen mainScreen].bounds;
        CGRect adjustedFrame = self.frame;
        
        if (adjustedFrame.origin.x < 0) {
            adjustedFrame.origin.x = 0;
        }
        if (adjustedFrame.origin.y < 0) {
            adjustedFrame.origin.y = 0;
        }
        if (adjustedFrame.origin.x + adjustedFrame.size.width > screenBounds.size.width) {
            adjustedFrame.origin.x = screenBounds.size.width - adjustedFrame.size.width;
        }
        if (adjustedFrame.origin.y + adjustedFrame.size.height > screenBounds.size.height) {
            adjustedFrame.origin.y = screenBounds.size.height - adjustedFrame.size.height;
        }
        
        [UIView animateWithDuration:0.3 animations:^{
            self.frame = adjustedFrame;
        }];
    }
}

- (void)closeButtonTapped {
    [UIView animateWithDuration:0.3 animations:^{
        self.alpha = 0;
    } completion:^(BOOL finished) {
        [self removeFromSuperview];
        [self cleanup];
    }];
}

- (void)cleanup {
    [_displayLink invalidate];
    _displayLink = nil;
    _stream = nil;
}

- (void)dealloc {
    [self cleanup];
}

@end

// Global preview window instance
static RTMPPreviewView *globalPreviewView = nil;

void rtmp_preview_show(RTMPStream *stream) {
    if (!stream) return;
    
    dispatch_async(dispatch_get_main_queue(), ^{
        if (globalPreviewView) {
            [globalPreviewView removeFromSuperview];
            globalPreviewView = nil;
        }
        
        CGRect previewFrame = CGRectMake(20, 60, 180, 240);
        globalPreviewView = [[RTMPPreviewView alloc] initWithStream:stream frame:previewFrame];
        
        UIWindow *window = [UIApplication sharedApplication].keyWindow;
        [window addSubview:globalPreviewView];
    });
}

void rtmp_preview_hide(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [globalPreviewView closeButtonTapped];
    });
}

void rtmp_preview_update_frame(CVImageBufferRef imageBuffer) {
    if (!globalPreviewView) return;
    [globalPreviewView updatePreviewImage:imageBuffer];
}