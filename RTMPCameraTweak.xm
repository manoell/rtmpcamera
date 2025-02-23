#import "rtmp_server_integration.h"
#import "rtmp_camera_compat.h"
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

// Interface for preview window
@interface RTMPPreviewWindow : UIWindow

@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, strong) UILabel *statsLabel;
@property (nonatomic, strong) CADisplayLink *displayLink;
@property (nonatomic, assign) CGFloat lastAlpha;

- (void)updateStats;
- (void)updatePosition;
- (void)handlePan:(UIPanGestureRecognizer *)gesture;
- (void)handleTap:(UITapGestureRecognizer *)gesture;

@end

@implementation RTMPPreviewWindow

- (instancetype)init {
    if (self = [super initWithFrame:CGRectMake(10, 50, 150, 80)]) {
        self.windowLevel = UIWindowLevelStatusBar + 1;
        self.backgroundColor = [UIColor colorWithWhite:0 alpha:0.8];
        self.layer.cornerRadius = 10;
        self.layer.borderWidth = 1;
        self.layer.borderColor = [UIColor colorWithWhite:0.8 alpha:0.3].CGColor;
        self.clipsToBounds = YES;
        
        // Status label
        self.statusLabel = [[UILabel alloc] initWithFrame:CGRectMake(5, 5, 140, 30)];
        self.statusLabel.textColor = [UIColor whiteColor];
        self.statusLabel.font = [UIFont boldSystemFontOfSize:12];
        self.statusLabel.text = @"RTMP: Waiting...";
        [self addSubview:self.statusLabel];
        
        // Stats label
        self.statsLabel = [[UILabel alloc] initWithFrame:CGRectMake(5, 35, 140, 40)];
        self.statsLabel.textColor = [UIColor whiteColor];
        self.statsLabel.font = [UIFont systemFontOfSize:10];
        self.statsLabel.numberOfLines = 2;
        [self addSubview:self.statsLabel];
        
        // Pan gesture for dragging
        UIPanGestureRecognizer *panGesture = [[UIPanGestureRecognizer alloc]
            initWithTarget:self action:@selector(handlePan:)];
        [self addGestureRecognizer:panGesture];
        
        // Double tap gesture for toggling visibility
        UITapGestureRecognizer *tapGesture = [[UITapGestureRecognizer alloc]
            initWithTarget:self action:@selector(handleTap:)];
        tapGesture.numberOfTapsRequired = 2;
        [self addGestureRecognizer:tapGesture];
        
        // Display link for stats updates
        self.displayLink = [CADisplayLink displayLinkWithTarget:self 
                                                     selector:@selector(updateStats)];
        [self.displayLink addToRunLoop:[NSRunLoop mainRunLoop] 
                             forMode:NSRunLoopCommonModes];
        
        self.lastAlpha = 1.0;
        
        // Start in visible state
        self.hidden = NO;
        self.alpha = 1.0;
        [self updatePosition];
    }
    return self;
}

- (void)updateStats {
    if (!rtmp_camera_compat_is_running()) {
        self.statusLabel.text = @"RTMP: Stopped";
        self.statusLabel.textColor = [UIColor redColor];
        return;
    }
    
    if (rtmp_camera_compat_is_publishing()) {
        self.statusLabel.text = @"RTMP: Streaming";
        self.statusLabel.textColor = [UIColor greenColor];
    } else {
        self.statusLabel.text = @"RTMP: Ready";
        self.statusLabel.textColor = [UIColor yellowColor];
    }
    
    int width, height;
    rtmp_camera_compat_get_resolution(&width, &height);
    float fps = rtmp_camera_compat_get_framerate();
    uint64_t frames = rtmp_camera_compat_get_frame_count();
    
    self.statsLabel.text = [NSString stringWithFormat:@"%dx%d @ %.1f fps\nFrames: %llu",
                           width, height, fps, frames];
}

- (void)updatePosition {
    CGRect screenBounds = [UIScreen mainScreen].bounds;
    CGRect frame = self.frame;
    
    // Keep window within screen bounds
    if (frame.origin.x < 0) {
        frame.origin.x = 0;
    } else if (frame.origin.x + frame.size.width > screenBounds.size.width) {
        frame.origin.x = screenBounds.size.width - frame.size.width;
    }
    
    if (frame.origin.y < 20) {
        frame.origin.y = 20;
    } else if (frame.origin.y + frame.size.height > screenBounds.size.height) {
        frame.origin.y = screenBounds.size.height - frame.size.height;
    }
    
    self.frame = frame;
}

- (void)handlePan:(UIPanGestureRecognizer *)gesture {
    CGPoint translation = [gesture translationInView:self];
    CGRect newFrame = self.frame;
    newFrame.origin.x += translation.x;
    newFrame.origin.y += translation.y;
    self.frame = newFrame;
    [gesture setTranslation:CGPointZero inView:self];
    
    if (gesture.state == UIGestureRecognizerStateEnded) {
        [self updatePosition];
    }
}

- (void)handleTap:(UITapGestureRecognizer *)gesture {
    if (gesture.state == UIGestureRecognizerStateEnded) {
        [UIView animateWithDuration:0.3 animations:^{
            if (self.alpha > 0.5) {
                self.alpha = 0.2;
            } else {
                self.alpha = self.lastAlpha;
            }
        }];
    }
}

- (void)dealloc {
    [self.displayLink invalidate];
    self.displayLink = nil;
}

@end

// Global variables
static RTMPPreviewWindow *previewWindow = nil;

%hook SpringBoard

- (void)applicationDidFinishLaunching:(id)application {
    %orig;
    
    // Initialize RTMP components
    rtmp_server_initialize();
    rtmp_camera_compat_initialize();
    
    // Create and show preview window
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1 * NSEC_PER_SEC),
                  dispatch_get_main_queue(), ^{
        previewWindow = [[RTMPPreviewWindow alloc] init];
    });
    
    // Start server and camera compatibility layer
    rtmp_server_start(1935);
    rtmp_camera_compat_start();
    
    // Register for memory warnings
    [[NSNotificationCenter defaultCenter] 
        addObserverForName:UIApplicationDidReceiveMemoryWarningNotification
                  object:nil
                   queue:[NSOperationQueue mainRunQueue]
              usingBlock:^(NSNotification *note) {
        // Stop and restart camera to free memory
        rtmp_camera_compat_stop();
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 1 * NSEC_PER_SEC),
                      dispatch_get_main_queue(), ^{
            rtmp_camera_compat_start();
        });
    }];
}

%end

%hook AVCaptureVideoDataOutput

- (void)setSampleBufferDelegate:(id)delegate queue:(dispatch_queue_t)queue {
    if (!%orig) {
        // If setting delegate fails, proceed with original
        %orig;
        return;
    }
    
    // If RTMP camera is active, intercept delegate
    if (rtmp_camera_compat_is_running()) {
        %orig([RTMPCameraCompatibility sharedInstance], queue);
    } else {
        %orig;
    }
}

%end

%hook AVCaptureConnection

- (void)setVideoOrientation:(AVCaptureVideoOrientation)orientation {
    if (rtmp_camera_compat_is_running()) {
        // Keep original orientation when RTMP is active
        return;
    }
    %orig;
}

%end

%hook AVCaptureDevice

- (BOOL)lockForConfiguration:(NSError **)error {
    if (rtmp_camera_compat_is_running()) {
        // Allow configuration changes when RTMP is active
        return YES;
    }
    return %orig;
}

- (void)unlockForConfiguration {
    if (!rtmp_camera_compat_is_running()) {
        %orig;
    }
}

%end

%ctor {
    @autoreleasepool {
        // Initialize hooks
        %init(SpringBoard);
        %init;
        
        NSLog(@"[RTMPCamera] Tweak initialized");
    }
}