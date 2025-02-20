#import "rtmp_preview.h"
#import "rtmp_log.h"
#import "rtmp_util.h"
#import <VideoToolbox/VideoToolbox.h>

#define PREVIEW_WIDTH 180.0f
#define PREVIEW_HEIGHT 320.0f

@interface RTMPPreviewView () {
    VTDecompressionSessionRef _decompressionSession;
    CMVideoFormatDescriptionRef _videoFormat;
    dispatch_queue_t _videoQueue;
    dispatch_queue_t _audioQueue;
    
    uint8_t* _sps;
    size_t _spsSize;
    uint8_t* _pps;
    size_t _ppsSize;
    BOOL _hasVideoFormat;
    
    float _lastVolume;
    BOOL _muted;
}

@property (nonatomic, strong) UIPanGestureRecognizer *panGesture;
@property (nonatomic, strong) UITapGestureRecognizer *doubleTapGesture;
@property (nonatomic, strong) UIPinchGestureRecognizer *pinchGesture;
@property (nonatomic, assign) CGPoint initialPosition;
@property (nonatomic, assign) CGFloat initialScale;

@end

@implementation RTMPPreviewView

+ (instancetype)sharedInstance {
    static RTMPPreviewView *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        CGRect screenBounds = [UIScreen mainScreen].bounds;
        CGRect frame = CGRectMake(screenBounds.size.width - PREVIEW_WIDTH - 20,
                                 40,
                                 PREVIEW_WIDTH,
                                 PREVIEW_HEIGHT);
        instance = [[RTMPPreviewView alloc] initWithFrame:frame];
    });
    return instance;
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor = [UIColor blackColor];
        self.layer.cornerRadius = 10.0f;
        self.clipsToBounds = YES;
        
        // Configurar layer de preview
        _previewLayer = [[AVSampleBufferDisplayLayer alloc] init];
        _previewLayer.frame = self.bounds;
        _previewLayer.videoGravity = AVLayerVideoGravityResizeAspect;
        [self.layer addSublayer:_previewLayer];
        
        // Configurar gestos
        [self setupGestures];
        
        // Configurar filas
        _videoQueue = dispatch_queue_create("com.rtmp.preview.video", DISPATCH_QUEUE_SERIAL);
        _audioQueue = dispatch_queue_create("com.rtmp.preview.audio", DISPATCH_QUEUE_SERIAL);
        
        // Estilo
        self.layer.shadowColor = [UIColor blackColor].CGColor;
        self.layer.shadowOffset = CGSizeMake(0, 2);
        self.layer.shadowRadius = 4.0;
        self.layer.shadowOpacity = 0.5;
        
        // Status inicial
        _hasVideoFormat = NO;
        _muted = NO;
        _lastVolume = 1.0;
        
        LOG_INFO("Preview view initialized with frame: %.0f,%.0f,%.0f,%.0f",
                 frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
    }
    return self;
}

- (void)setupGestures {
    // Pan gesture
    _panGesture = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePan:)];
    [self addGestureRecognizer:_panGesture];
    
    // Double tap gesture
    _doubleTapGesture = [[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(handleDoubleTap:)];
    _doubleTapGesture.numberOfTapsRequired = 2;
    [self addGestureRecognizer:_doubleTapGesture];
    
    // Pinch gesture
    _pinchGesture = [[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(handlePinch:)];
    [self addGestureRecognizer:_pinchGesture];
}

- (void)handlePan:(UIPanGestureRecognizer *)gesture {
    CGPoint translation = [gesture translationInView:self.superview];
    
    if (gesture.state == UIGestureRecognizerStateBegan) {
        _initialPosition = self.center;
    }
    
    CGPoint newCenter = CGPointMake(_initialPosition.x + translation.x,
                                   _initialPosition.y + translation.y);
    
    CGRect bounds = [UIScreen mainScreen].bounds;
    newCenter.x = MAX(self.frame.size.width/2,
                     MIN(newCenter.x, bounds.size.width - self.frame.size.width/2));
    newCenter.y = MAX(self.frame.size.height/2,
                     MIN(newCenter.y, bounds.size.height - self.frame.size.height/2));
    
    self.center = newCenter;
}

- (void)handleDoubleTap:(UITapGestureRecognizer *)gesture {
    [UIView animateWithDuration:0.3 animations:^{
        _muted = !_muted;
        self.alpha = _muted ? 0.2 : 1.0;
    }];
}

- (void)handlePinch:(UIPinchGestureRecognizer *)gesture {
    if (gesture.state == UIGestureRecognizerStateBegan) {
        _initialScale = self.transform.a;
    }
    
    CGFloat scale = _initialScale * gesture.scale;
    scale = MAX(0.5, MIN(scale, 2.0));
    
    self.transform = CGAffineTransformMakeScale(scale, scale);
}

- (void)setupDecompressionSessionWithSPS:(const uint8_t *)sps spsSize:(size_t)spsSize
                                    PPS:(const uint8_t *)pps ppsSize:(size_t)ppsSize {
    if (_decompressionSession) {
        VTDecompressionSessionInvalidate(_decompressionSession);
        _decompressionSession = NULL;
    }
    
    if (_videoFormat) {
        CFRelease(_videoFormat);
        _videoFormat = NULL;
    }
    
    const uint8_t* parameterSetPointers[2] = { sps, pps };
    const size_t parameterSetSizes[2] = { spsSize, ppsSize };
    
    OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
        kCFAllocatorDefault,
        2,
        parameterSetPointers,
        parameterSetSizes,
        4,
        &_videoFormat
    );
    
    if (status != noErr) {
        LOG_ERROR("Failed to create video format description: %d", (int)status);
        return;
    }
    
    // Setup decompression properties
    VTDecompressionOutputCallbackRecord callback;
    callback.decompressionOutputCallback = DecompressionSessionDecodeCallback;
    callback.decompressionOutputRefCon = (__bridge void *)self;
    
    NSDictionary* attributes = @{
        (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        (NSString*)kCVPixelBufferMetalCompatibilityKey: @YES,
    };
    
    status = VTDecompressionSessionCreate(
        kCFAllocatorDefault,
        _videoFormat,
        NULL,
        (__bridge CFDictionaryRef)attributes,
        &callback,
        &_decompressionSession
    );
    
    if (status != noErr) {
        LOG_ERROR("Failed to create decompression session: %d", (int)status);
        return;
    }
    
    _hasVideoFormat = YES;
    LOG_INFO("Video decompression session created");
}

- (void)showPreview {
    if (!_isVisible) {
        _isVisible = YES;
        
        dispatch_async(dispatch_get_main_queue(), ^{
            UIWindow *window = [[UIApplication sharedApplication] keyWindow];
            if (window) {
                [window addSubview:self];
                LOG_INFO("Preview window added to keyWindow");
            } else {
                LOG_ERROR("Failed to get keyWindow");
            }
        });
    }
}

- (void)hidePreview {
    if (_isVisible) {
        _isVisible = NO;
        
        dispatch_async(dispatch_get_main_queue(), ^{
            [self removeFromSuperview];
            LOG_INFO("Preview window removed");
        });
    }
}

- (void)displayDecodedFrame:(CVImageBufferRef)imageBuffer withTimestamp:(CMTime)timestamp {
    if (!imageBuffer) return;
    
    @try {
        CMSampleBufferRef sampleBuffer = NULL;
        CMSampleTimingInfo timing = {CMTimeMake(1, 1000), timestamp, timestamp};
        
        OSStatus status = CMSampleBufferCreateForImageBuffer(
            kCFAllocatorDefault,
            imageBuffer,
            true,
            NULL,
            NULL,
            _videoFormat,
            &timing,
            &sampleBuffer
        );
        
        if (status != noErr) {
            LOG_ERROR("Failed to create sample buffer: %d", (int)status);
            return;
        }
        
        if (_previewLayer.status == AVQueuedSampleBufferRenderingStatusFailed) {
            [_previewLayer flush];
        }
        
        [_previewLayer enqueueSampleBuffer:sampleBuffer];
        CFRelease(sampleBuffer);
        
    } @catch (NSException *exception) {
        LOG_ERROR("Exception in displayDecodedFrame: %s", [exception.description UTF8String]);
    }
}

- (void)processVideoData:(uint8_t *)data length:(size_t)length timestamp:(uint32_t)timestamp {
    if (!data || length == 0) return;
    
    dispatch_async(_videoQueue, ^{
        // Parse NAL units
        if (length < 4) return;
        
        uint8_t naluType = data[4] & 0x1F;
        
        // Process SPS/PPS
        if (naluType == 7) { // SPS
            free(_sps);
            _spsSize = length - 4;
            _sps = malloc(_spsSize);
            if (_sps) {
                memcpy(_sps, data + 4, _spsSize);
                
                if (_pps) {
                    [self setupDecompressionSessionWithSPS:_sps spsSize:_spsSize
                                                     PPS:_pps ppsSize:_ppsSize];
                }
            }
        }
        else if (naluType == 8) { // PPS
            free(_pps);
            _ppsSize = length - 4;
            _pps = malloc(_ppsSize);
            if (_pps) {
                memcpy(_pps, data + 4, _ppsSize);
                
                if (_sps) {
                    [self setupDecompressionSessionWithSPS:_sps spsSize:_spsSize
                                                     PPS:_pps ppsSize:_ppsSize];
                }
            }
        }
        else if (_hasVideoFormat && (naluType == 1 || naluType == 5)) { // IDR or non-IDR
            CMBlockBufferRef blockBuffer = NULL;
            OSStatus status = CMBlockBufferCreateWithMemoryBlock(
                kCFAllocatorDefault,
                data,
                length,
                kCFAllocatorNull,
                NULL,
                0,
                length,
                0,
                &blockBuffer
            );
            
            if (status != noErr) {
                LOG_ERROR("Failed to create block buffer: %d", (int)status);
                return;
            }
            
            CMSampleBufferRef sampleBuffer = NULL;
            const size_t sampleSizesArray[] = {length};
            status = CMSampleBufferCreateReady(
                kCFAllocatorDefault,
                blockBuffer,
                _videoFormat,
                1,
                0,
                NULL,
                1,
                sampleSizesArray,
                &sampleBuffer
            );
            
            if (status != noErr) {
                LOG_ERROR("Failed to create sample buffer: %d", (int)status);
                CFRelease(blockBuffer);
                return;
            }
            
            VTDecodeFrameFlags flags = kVTDecodeFrame_EnableAsynchronousDecompression;
            VTDecompressionSessionDecodeFrame(
                _decompressionSession,
                sampleBuffer,
                flags,
                NULL,
                NULL
            );
            
            CFRelease(sampleBuffer);
            CFRelease(blockBuffer);
        }
    });
}

- (void)processAudioData:(uint8_t *)data length:(size_t)length timestamp:(uint32_t)timestamp {
    if (!data || length == 0 || _muted) return;
    
    dispatch_async(_audioQueue, ^{
        // TODO: Implementar processamento de áudio
        LOG_DEBUG("Received audio data: %zu bytes", length);
    });
}

- (void)dealloc {
    if (_decompressionSession) {
        VTDecompressionSessionInvalidate(_decompressionSession);
        _decompressionSession = NULL;
    }
    
    if (_videoFormat) {
        CFRelease(_videoFormat);
        _videoFormat = NULL;
    }
    
    free(_sps);
    free(_pps);
    
    LOG_INFO("Preview view destroyed");
}

@end

// Interface C
void rtmp_preview_init(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [[RTMPPreviewView sharedInstance] showPreview];
    });
}

void rtmp_preview_show(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [[RTMPPreviewView sharedInstance] showPreview];
    });
}

void rtmp_preview_hide(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        [[RTMPPreviewView sharedInstance] hidePreview];
    });
}

void rtmp_preview_process_video(const uint8_t* data, size_t length, uint32_t timestamp) {
    [[RTMPPreviewView sharedInstance] processVideoData:(uint8_t*)data length:length timestamp:timestamp];
}

void rtmp_preview_process_audio(const uint8_t* data, size_t length, uint32_t timestamp) {
    [[RTMPPreviewView sharedInstance] processAudioData:(uint8_t*)data length:length timestamp:timestamp];
}