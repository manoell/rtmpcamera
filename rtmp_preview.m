#import "rtmp_preview.h"
#import "rtmp_quality.h"
#import <AVFoundation/AVFoundation.h>

@interface RTMPPreviewWindow () <UIGestureRecognizerDelegate>

@property (nonatomic, strong) UIView *previewView;
@property (nonatomic, strong) UIView *controlsView;
@property (nonatomic, strong) UILabel *statsLabel;
@property (nonatomic, strong) UIButton *closeButton;
@property (nonatomic, strong) UIButton *qualityButton;
@property (nonatomic, strong) UISegmentedControl *qualityControl;
@property (nonatomic, strong) UIPanGestureRecognizer *panGesture;
@property (nonatomic, strong) UITapGestureRecognizer *tapGesture;
@property (nonatomic, strong) RTMPStream *currentStream;
@property (nonatomic, weak) id<RTMPPreviewDelegate> delegate;
@property (nonatomic, strong) NSTimer *statsUpdateTimer;
@property (nonatomic, assign) CGPoint initialPosition;
@property (nonatomic, assign) CGSize initialSize;

@end

@implementation RTMPPreviewWindow

- (instancetype)init {
    self = [super init];
    if (self) {
        self.windowLevel = UIWindowLevelStatusBar + 1;
        self.backgroundColor = [UIColor clearColor];
        self.layer.cornerRadius = 10;
        self.layer.masksToBounds = true;
        
        // Default settings
        self.draggable = YES;
        self.showStats = YES;
        self.previewScale = 1.0;
        
        [self setupUI];
        [self setupGestures];
    }
    return self;
}

- (void)setupUI {
    // Preview view
    self.previewView = [[UIView alloc] init];
    self.previewView.backgroundColor = [UIColor blackColor];
    [self addSubview:self.previewView];
    
    // Controls view
    self.controlsView = [[UIView alloc] init];
    self.controlsView.backgroundColor = [UIColor colorWithWhite:0 alpha:0.7];
    [self addSubview:self.controlsView];
    
    // Stats label
    self.statsLabel = [[UILabel alloc] init];
    self.statsLabel.textColor = [UIColor whiteColor];
    self.statsLabel.font = [UIFont systemFontOfSize:10];
    self.statsLabel.numberOfLines = 0;
    [self.controlsView addSubview:self.statsLabel];
    
    // Close button
    self.closeButton = [UIButton buttonWithType:UIButtonTypeCustom];
    [self.closeButton setTitle:@"×" forState:UIControlStateNormal];
    [self.closeButton addTarget:self action:@selector(closeButtonTapped) forControlEvents:UIControlEventTouchUpInside];
    [self.controlsView addSubview:self.closeButton];
    
    // Quality button
    self.qualityButton = [UIButton buttonWithType:UIButtonTypeCustom];
    [self.qualityButton setImage:[UIImage systemImageNamed:@"gear"] forState:UIControlStateNormal];
    [self.qualityButton addTarget:self action:@selector(qualityButtonTapped) forControlEvents:UIControlEventTouchUpInside];
    [self.controlsView addSubview:self.qualityButton];
    
    // Quality control
    self.qualityControl = [[UISegmentedControl alloc] initWithItems:@[@"Low", @"Med", @"High", @"Auto"]];
    self.qualityControl.selectedSegmentIndex = 3; // Auto by default
    [self.qualityControl addTarget:self action:@selector(qualityChanged:) forControlEvents:UIControlEventValueChanged];
    self.qualityControl.hidden = YES;
    [self.controlsView addSubview:self.qualityControl];
}

- (void)setupGestures {
    // Pan gesture for dragging
    self.panGesture = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePan:)];
    self.panGesture.delegate = self;
    [self addGestureRecognizer:self.panGesture];
    
    // Tap gesture for showing/hiding controls
    self.tapGesture = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(handleTap:)];
    [self addGestureRecognizer:self.tapGesture];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    
    CGFloat controlsHeight = 44;
    CGFloat padding = 8;
    
    // Layout preview view
    self.previewView.frame = self.bounds;
    
    // Layout controls view
    self.controlsView.frame = CGRectMake(0, 
                                        self.bounds.size.height - controlsHeight,
                                        self.bounds.size.width, 
                                        controlsHeight);
    
    // Layout stats label
    self.statsLabel.frame = CGRectMake(padding,
                                      0,
                                      self.controlsView.bounds.size.width - 100,
                                      controlsHeight);
    
    // Layout close button
    self.closeButton.frame = CGRectMake(self.controlsView.bounds.size.width - 44,
                                       0,
                                       44,
                                       44);
    
    // Layout quality button
    self.qualityButton.frame = CGRectMake(self.controlsView.bounds.size.width - 88,
                                         0,
                                         44,
                                         44);
    
    // Layout quality control
    self.qualityControl.frame = CGRectMake(padding,
                                          -44,
                                          self.controlsView.bounds.size.width - padding * 2,
                                          32);
}

#pragma mark - Public Methods

- (void)show {
    self.hidden = NO;
    
    if (!self.statsUpdateTimer && self.showStats) {
        self.statsUpdateTimer = [NSTimer scheduledTimerWithTimeInterval:1.0 
                                                               target:self
                                                             selector:@selector(updateStatsDisplay)
                                                             userInfo:nil
                                                              repeats:YES];
    }
}

- (void)hide {
    self.hidden = YES;
    [self.statsUpdateTimer invalidate];
    self.statsUpdateTimer = nil;
}

- (void)updateWithStream:(RTMPStream *)stream {
    self.currentStream = stream;
    
    // Update preview layer
    if ([stream.previewLayer isKindOfClass:[AVCaptureVideoPreviewLayer class]]) {
        AVCaptureVideoPreviewLayer *previewLayer = (AVCaptureVideoPreviewLayer *)stream.previewLayer;
        previewLayer.frame = self.previewView.bounds;
        [self.previewView.layer addSublayer:previewLayer];
    }
}

- (void)setPosition:(CGPoint)position {
    CGRect frame = self.frame;
    frame.origin = position;
    self.frame = frame;
    
    if ([self.delegate respondsToSelector:@selector(previewDidMove:)]) {
        [self.delegate previewDidMove:self];
    }
}

- (void)setSize:(CGSize)size {
    CGRect frame = self.frame;
    frame.size = size;
    self.frame = frame;
    
    if ([self.delegate respondsToSelector:@selector(previewDidResize:)]) {
        [self.delegate previewDidResize:self];
    }
}

- (void)setStatsVisible:(BOOL)visible {
    self.showStats = visible;
    self.statsLabel.hidden = !visible;
    
    if (visible && !self.statsUpdateTimer) {
        self.statsUpdateTimer = [NSTimer scheduledTimerWithTimeInterval:1.0
                                                               target:self
                                                             selector:@selector(updateStatsDisplay)
                                                             userInfo:nil
                                                              repeats:YES];
    } else if (!visible && self.statsUpdateTimer) {
        [self.statsUpdateTimer invalidate];
        self.statsUpdateTimer = nil;
    }
}

- (void)updateStats:(NSDictionary *)stats {
    if (!self.showStats) return;
    
    NSMutableString *statsText = [NSMutableString string];
    [statsText appendFormat:@"Bitrate: %@ kbps\n", stats[@"bitrate"]];
    [statsText appendFormat:@"FPS: %@\n", stats[@"fps"]];
    [statsText appendFormat:@"Resolution: %@\n", stats[@"resolution"]];
    [statsText appendFormat:@"Quality: %@", stats[@"quality"]];
    
    self.statsLabel.text = statsText;
}

- (void)enableStreamControls:(BOOL)enable {
    self.qualityButton.enabled = enable;
    self.qualityControl.enabled = enable;
}

- (void)setStreamQuality:(RTMPQualityLevel)quality {
    self.qualityControl.selectedSegmentIndex = quality;
}

#pragma mark - Private Methods

- (void)closeButtonTapped {
    [self hide];
}

- (void)qualityButtonTapped {
    [UIView animateWithDuration:0.3 animations:^{
        self.qualityControl.hidden = !self.qualityControl.hidden;
    }];
}

- (void)qualityChanged:(UISegmentedControl *)sender {
    RTMPQualityLevel quality = (RTMPQualityLevel)sender.selectedSegmentIndex;
    
    if ([self.delegate respondsToSelector:@selector(preview:didChangeQuality:)]) {
        [self.delegate preview:self didChangeQuality:quality];
    }
    
    [UIView animateWithDuration:0.3 animations:^{
        self.qualityControl.hidden = YES;
    }];
}

- (void)updateStatsDisplay {
    if (!self.currentStream) return;
    
    RTMPQualityStats *stats = rtmp_quality_get_stats(self.currentStream.qualityController);
    if (!stats) return;
    
    NSDictionary *statsDict = @{
        @"bitrate": @(stats->currentBitrate / 1000),
        @"fps": @(stats->currentFPS),
        @"resolution": [NSString stringWithFormat:@"%dx%d", 
                       self.currentStream.width,
                       self.currentStream.height],
        @"quality": @[@"Low", @"Medium", @"High", @"Auto"][self.qualityControl.selectedSegmentIndex]
    };
    
    [self updateStats:statsDict];
}

#pragma mark - Gesture Handlers

- (void)handlePan:(UIPanGestureRecognizer *)gesture {
    if (!self.draggable) return;
    
    switch (gesture.state) {
        case UIGestureRecognizerStateBegan:
            self.initialPosition = self.frame.origin;
            break;
            
        case UIGestureRecognizerStateChanged: {
            CGPoint translation = [gesture translationInView:self];
            CGPoint newPosition = CGPointMake(self.initialPosition.x + translation.x,
                                            self.initialPosition.y + translation.y);
            [self setPosition:newPosition];
            break;
        }
            
        default:
            break;
    }
}

- (void)handleTap:(UITapGestureRecognizer *)gesture {
    if ([self.delegate respondsToSelector:@selector(previewDidTap:)]) {
        [self.delegate previewDidTap:self];
    }
    
    // Toggle controls visibility
    [UIView animateWithDuration:0.3 animations:^{
        self.controlsView.alpha = self.controlsView.alpha > 0 ? 0 : 1;
    }];
}

#pragma mark - UIGestureRecognizerDelegate

- (BOOL)gestureRecognizerShouldBegin:(UIGestureRecognizer *)gestureRecognizer {
    if (gestureRecognizer == self.panGesture) {
        return self.draggable;
    }
    return YES;
}

@end