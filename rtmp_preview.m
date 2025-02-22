#import "rtmp_preview.h"
#import "rtmp_utils.h"
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>

@interface RTMPPreviewWindow : UIWindow
@property (nonatomic, assign) BOOL isDragging;
@property (nonatomic, assign) CGPoint dragOffset;
@end

@implementation RTMPPreviewWindow
- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event {
    UITouch *touch = [touches anyObject];
    CGPoint location = [touch locationInView:self];
    self.isDragging = YES;
    self.dragOffset = location;
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event {
    if (!self.isDragging) return;
    
    UITouch *touch = [touches anyObject];
    CGPoint location = [touch locationInView:self];
    CGPoint newCenter = CGPointMake(
        self.center.x + (location.x - self.dragOffset.x),
        self.center.y + (location.y - self.dragOffset.y)
    );
    
    // Keep window within screen bounds
    CGRect screen = [[UIScreen mainScreen] bounds];
    CGFloat minX = self.frame.size.width / 2;
    CGFloat maxX = screen.size.width - minX;
    CGFloat minY = self.frame.size.height / 2;
    CGFloat maxY = screen.size.height - minY;
    
    newCenter.x = fmax(minX, fmin(maxX, newCenter.x));
    newCenter.y = fmax(minY, fmin(maxY, newCenter.y));
    
    self.center = newCenter;
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event {
    self.isDragging = NO;
}

- (void)touchesCancelled:(NSSet *)touches withEvent:(UIEvent *)event {
    self.isDragging = NO;
}
@end

@interface RTMPPreviewController () {
    RTMPPreviewWindow *_previewWindow;
    UIView *_previewView;
    AVSampleBufferDisplayLayer *_previewLayer;
    UILabel *_statsLabel;
    UIButton *_closeButton;
    UIButton *_minimizeButton;
    dispatch_queue_t _previewQueue;
    BOOL _isMinimized;
    CGRect _normalFrame;
    NSTimer *_statsUpdateTimer;
}

@property (nonatomic, strong) NSMutableDictionary *streamStats;
@end

@implementation RTMPPreviewController

- (id)init {
    self = [super init];
    if (self) {
        _previewQueue = dispatch_queue_create("com.rtmp.preview", DISPATCH_QUEUE_SERIAL);
        _streamStats = [NSMutableDictionary dictionary];
        [self setupPreviewWindow];
        [self setupStatisticsTimer];
    }
    return self;
}

- (void)setupPreviewWindow {
    // Create preview window
    _previewWindow = [[RTMPPreviewWindow alloc] initWithFrame:CGRectMake(0, 0, 320, 240)];
    _previewWindow.windowLevel = UIWindowLevelStatusBar + 1;
    _previewWindow.backgroundColor = [UIColor blackColor];
    _previewWindow.layer.cornerRadius = 10;
    _previewWindow.layer.masksToBounds = YES;
    _previewWindow.center = CGPointMake(
        [[UIScreen mainScreen] bounds].size.width - 170,
        [[UIScreen mainScreen] bounds].size.height - 130
    );
    _normalFrame = _previewWindow.frame;
    
    // Setup preview layer
    _previewView = [[UIView alloc] initWithFrame:_previewWindow.bounds];
    _previewView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [_previewWindow addSubview:_previewView];
    
    _previewLayer = [[AVSampleBufferDisplayLayer alloc] init];
    _previewLayer.frame = _previewView.bounds;
    _previewLayer.videoGravity = AVLayerVideoGravityResizeAspect;
    [_previewView.layer addSublayer:_previewLayer];
    
    // Setup stats label
    _statsLabel = [[UILabel alloc] initWithFrame:CGRectMake(5, 5, 310, 60)];
    _statsLabel.numberOfLines = 3;
    _statsLabel.font = [UIFont systemFontOfSize:10];
    _statsLabel.textColor = [UIColor whiteColor];
    _statsLabel.backgroundColor = [UIColor colorWithWhite:0 alpha:0.5];
    _statsLabel.layer.cornerRadius = 5;
    _statsLabel.layer.masksToBounds = YES;
    [_previewWindow addSubview:_statsLabel];
    
    // Setup control buttons
    _closeButton = [UIButton buttonWithType:UIButtonTypeCustom];
    _closeButton.frame = CGRectMake(_previewWindow.frame.size.width - 30, 5, 25, 25);
    [_closeButton setTitle:@"×" forState:UIControlStateNormal];
    [_closeButton addTarget:self action:@selector(hidePreview) forControlEvents:UIControlEventTouchUpInside];
    [_previewWindow addSubview:_closeButton];
    
    _minimizeButton = [UIButton buttonWithType:UIButtonTypeCustom];
    _minimizeButton.frame = CGRectMake(_previewWindow.frame.size.width - 60, 5, 25, 25);
    [_minimizeButton setTitle:@"_" forState:UIControlStateNormal];
    [_minimizeButton addTarget:self action:@selector(toggleMinimize) forControlEvents:UIControlEventTouchUpInside];
    [_previewWindow addSubview:_minimizeButton];
}

- (void)setupStatisticsTimer {
    _statsUpdateTimer = [NSTimer scheduledTimerWithTimeInterval:1.0
                                                       target:self
                                                     selector:@selector(updateStatistics)
                                                     userInfo:nil
                                                      repeats:YES];
}

- (void)showPreview {
    dispatch_async(dispatch_get_main_queue(), ^{
        self->_previewWindow.hidden = NO;
        [self->_previewWindow makeKeyAndVisible];
    });
}

- (void)hidePreview {
    dispatch_async(dispatch_get_main_queue(), ^{
        self->_previewWindow.hidden = YES;
    });
}

- (void)toggleMinimize {
    _isMinimized = !_isMinimized;
    
    [UIView animateWithDuration:0.3 animations:^{
        if (self->_isMinimized) {
            CGRect minimizedFrame = self->_previewWindow.frame;
            minimizedFrame.size.height = 40;
            self->_previewWindow.frame = minimizedFrame;
            self->_previewView.alpha = 0;
            self->_statsLabel.alpha = 0;
        } else {
            self->_previewWindow.frame = self->_normalFrame;
            self->_previewView.alpha = 1;
            self->_statsLabel.alpha = 1;
        }
    }];
}

- (void)updateFrame:(CMSampleBufferRef)sampleBuffer {
    if (!sampleBuffer || _previewWindow.hidden || _isMinimized) return;
    
    CFRetain(sampleBuffer);
    dispatch_async(_previewQueue, ^{
        if (self->_previewLayer.status == AVQueuedSampleBufferRenderingStatusFailed) {
            [self->_previewLayer flush];
        }
        
        [self->_previewLayer enqueueSampleBuffer:sampleBuffer];
        CFRelease(sampleBuffer);
        
        // Update frame statistics
        dispatch_async(dispatch_get_main_queue(), ^{
            NSInteger frameCount = [self.streamStats[@"frameCount"] integerValue] + 1;
            self.streamStats[@"frameCount"] = @(frameCount);
            
            CMTime presentationTime = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
            self.streamStats[@"lastFrameTime"] = @(CMTimeGetSeconds(presentationTime));
        });
    });
}

- (void)updateStatistics {
    if (_previewWindow.hidden || _isMinimized) return;
    
    NSInteger frameCount = [self.streamStats[@"frameCount"] integerValue];
    NSTimeInterval lastFrameTime = [self.streamStats[@"lastFrameTime"] doubleValue];
    
    // Calculate FPS
    static NSInteger lastFrameCount = 0;
    static NSTimeInterval lastCalculationTime = 0;
    
    NSTimeInterval currentTime = [[NSDate date] timeIntervalSince1970];
    if (currentTime - lastCalculationTime >= 1.0) {
        NSInteger fps = frameCount - lastFrameCount;
        self.streamStats[@"fps"] = @(fps);
        
        lastFrameCount = frameCount;
        lastCalculationTime = currentTime;
    }
    
    // Get stream quality metrics
    float bitrate = [self.streamStats[@"bitrate"] floatValue];
    float quality = [self.streamStats[@"quality"] floatValue];
    NSInteger droppedFrames = [self.streamStats[@"droppedFrames"] integerValue];
    
    // Update stats display
    dispatch_async(dispatch_get_main_queue(), ^{
        NSString *statsText = [NSString stringWithFormat:
            @"FPS: %d | Bitrate: %.1f Mbps\n"
            @"Quality: %.1f%% | Dropped: %ld\n"
            @"Buffer: %.1f s",
            [self.streamStats[@"fps"] intValue],
            bitrate / 1000000.0,
            quality * 100,
            (long)droppedFrames,
            [self.streamStats[@"bufferDuration"] floatValue]
        ];
        
        self->_statsLabel.text = statsText;
    });
}

- (void)updateStreamMetrics:(NSDictionary *)metrics {
    [self.streamStats addEntriesFromDictionary:metrics];
}

- (void)dealloc {
    [_statsUpdateTimer invalidate];
    _statsUpdateTimer = nil;
    
    [_previewLayer flush];
    [self hidePreview];
}

@end