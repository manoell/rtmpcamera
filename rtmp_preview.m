// rtmp_preview.m
#import "rtmp_preview.h"
#import <CoreVideo/CoreVideo.h>

@interface RTMPPreviewView () {
    AVSampleBufferDisplayLayer* _displayLayer;
    dispatch_queue_t _previewQueue;
    NSInteger _frameCount;
    NSTimeInterval _lastFPSCalculation;
    float _currentFPS;
    BOOL _isPreviewRunning;
}

@property (nonatomic, strong) CADisplayLink* fpsTimer;

@end

@implementation RTMPPreviewView

- (id)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        [self commonInit];
    }
    return self;
}

- (id)initWithCoder:(NSCoder *)coder {
    self = [super initWithCoder:coder];
    if (self) {
        [self commonInit];
    }
    return self;
}

- (void)commonInit {
    // Configuração inicial
    self.backgroundColor = [UIColor blackColor];
    self.layer.cornerRadius = 8.0;
    self.clipsToBounds = YES;
    
    // Configuração da layer de display
    _displayLayer = [[AVSampleBufferDisplayLayer alloc] init];
    _displayLayer.videoGravity = AVLayerVideoGravityResizeAspect;
    _displayLayer.opaque = YES;
    [self.layer addSublayer:_displayLayer];
    
    // Queue dedicada para preview com alta prioridade
    _previewQueue = dispatch_queue_create("com.rtmpcamera.preview", 
                                        DISPATCH_QUEUE_SERIAL_WITH_AUTORELEASE_POOL);
    dispatch_set_target_queue(_previewQueue, 
                            dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
    
    // Inicializa variáveis
    _frameCount = 0;
    _lastFPSCalculation = CACurrentMediaTime();
    _currentFPS = 0.0;
    _isPreviewRunning = NO;
    
    // Adiciona gesture recognizer para arrastar
    UIPanGestureRecognizer *panGesture = [[UIPanGestureRecognizer alloc] 
                                         initWithTarget:self 
                                         action:@selector(handlePan:)];
    [self addGestureRecognizer:panGesture];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    _displayLayer.frame = self.bounds;
}

- (void)setupPreviewWithSize:(CGSize)size {
    _streamSize = size;
    dispatch_async(dispatch_get_main_queue(), ^{
        // Atualiza o tamanho da view mantendo aspect ratio
        float aspectRatio = size.width / size.height;
        float viewWidth = self.bounds.size.width;
        float viewHeight = viewWidth / aspectRatio;
        CGRect newFrame = self.frame;
        newFrame.size.height = viewHeight;
        self.frame = newFrame;
        
        [self setNeedsLayout];
    });
}

- (void)startPreview {
    if (_isPreviewRunning) return;
    
    _isPreviewRunning = YES;
    
    // Inicia timer para cálculo de FPS
    self.fpsTimer = [CADisplayLink displayLinkWithTarget:self 
                                              selector:@selector(calculateFPS)];
    [self.fpsTimer addToRunLoop:[NSRunLoop mainRunLoop] 
                       forMode:NSRunLoopCommonModes];
    
    rtmp_log(RTMP_LOG_INFO, "Preview started");
}

- (void)stopPreview {
    if (!_isPreviewRunning) return;
    
    _isPreviewRunning = NO;
    [self.fpsTimer invalidate];
    self.fpsTimer = nil;
    
    [_displayLayer flush];
    
    rtmp_log(RTMP_LOG_INFO, "Preview stopped");
}

- (void)displayVideoFrame:(CMSampleBufferRef)sampleBuffer {
    if (!_isPreviewRunning || !sampleBuffer) return;
    
    CFRetain(sampleBuffer);
    dispatch_async(_previewQueue, ^{
        if (_displayLayer.status == AVQueuedSampleBufferRenderingStatusFailed) {
            [_displayLayer flush];
        }
        
        if (_displayLayer.readyForMoreMediaData) {
            [_displayLayer enqueueSampleBuffer:sampleBuffer];
            _frameCount++;
        }
        
        CFRelease(sampleBuffer);
    });
}

- (void)calculateFPS {
    NSTimeInterval now = CACurrentMediaTime();
    NSTimeInterval elapsed = now - _lastFPSCalculation;
    
    if (elapsed >= 1.0) {
        _currentFPS = _frameCount / elapsed;
        _frameCount = 0;
        _lastFPSCalculation = now;
        
        // Log FPS a cada segundo
        rtmp_log(RTMP_LOG_DEBUG, "Preview FPS: %.2f", _currentFPS);
    }
}

- (void)handlePan:(UIPanGestureRecognizer *)gesture {
    CGPoint translation = [gesture translationInView:self.superview];
    
    switch (gesture.state) {
        case UIGestureRecognizerStateChanged: {
            CGPoint center = self.center;
            center.x += translation.x;
            center.y += translation.y;
            
            // Mantém dentro dos limites da tela
            CGRect bounds = self.superview.bounds;
            float halfWidth = self.bounds.size.width / 2;
            float halfHeight = self.bounds.size.height / 2;
            
            center.x = MAX(halfWidth, MIN(bounds.size.width - halfWidth, center.x));
            center.y = MAX(halfHeight, MIN(bounds.size.height - halfHeight, center.y));
            
            self.center = center;
            [gesture setTranslation:CGPointZero inView:self.superview];
            break;
        }
        default:
            break;
    }
}

#pragma mark - Properties

- (float)streamFPS {
    return _currentFPS;
}

- (void)setStreamFPS:(float)streamFPS {
    _currentFPS = streamFPS;
}

@end