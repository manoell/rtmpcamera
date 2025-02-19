#import "rtmp_preview.h"
#import "rtmp_log.h"
#import <VideoToolbox/VideoToolbox.h>

static const float PREVIEW_WIDTH = 180.0f;
static const float PREVIEW_HEIGHT = 320.0f;

static void DecompressionSessionDecodeCallback(void *decompressionOutputRefCon,
                                             void *sourceFrameRefCon,
                                             OSStatus status,
                                             VTDecodeInfoFlags infoFlags,
                                             CVImageBufferRef imageBuffer,
                                             CMTime presentationTimeStamp,
                                             CMTime presentationDuration) {
    if (status != noErr) {
        LOG_ERROR("Decompression error: %d", (int)status);
        return;
    }
    
    RTMPPreviewView *preview = (__bridge RTMPPreviewView *)decompressionOutputRefCon;
    [preview displayDecodedFrame:imageBuffer withTimestamp:presentationTimeStamp];
}

@interface RTMPPreviewView () {
    VTDecompressionSessionRef _decompressionSession;
    CMVideoFormatDescriptionRef _videoFormat;
    dispatch_queue_t _decompressionQueue;
    NSMutableArray *_audioQueue;
    
    uint8_t *_sps;
    size_t _spsSize;
    uint8_t *_pps;
    size_t _ppsSize;
    BOOL _hasVideoFormat;
}

@property (nonatomic, strong) UIPanGestureRecognizer *panGesture;
@property (nonatomic, assign) CGPoint initialPosition;

@end

@implementation RTMPPreviewView

+ (instancetype)sharedInstance {
    static RTMPPreviewView *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[RTMPPreviewView alloc] initWithFrame:CGRectMake(20, 40, PREVIEW_WIDTH, PREVIEW_HEIGHT)];
    });
    return instance;
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor = [UIColor blackColor];
        self.layer.cornerRadius = 10.0f;
        self.clipsToBounds = YES;
        
        _previewLayer = [[AVSampleBufferDisplayLayer alloc] init];
        _previewLayer.frame = self.bounds;
        _previewLayer.videoGravity = AVLayerVideoGravityResizeAspect;
        [self.layer addSublayer:_previewLayer];
        
        _panGesture = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePan:)];
        [self addGestureRecognizer:_panGesture];
        
        _decompressionQueue = dispatch_queue_create("com.rtmp.preview.decompress", DISPATCH_QUEUE_SERIAL);
        _audioQueue = [NSMutableArray array];
        
        _hasVideoFormat = NO;
        
        self.layer.shadowColor = [UIColor blackColor].CGColor;
        self.layer.shadowOffset = CGSizeMake(0, 2);
        self.layer.shadowRadius = 4.0;
        self.layer.shadowOpacity = 0.5;
    }
    return self;
}

- (void)displayDecodedFrame:(CVImageBufferRef)imageBuffer withTimestamp:(CMTime)timestamp {
    if (!imageBuffer) return;
    
    CMSampleBufferRef sampleBuffer = NULL;
    CMSampleTimingInfo timing = {CMTimeMake(1, 1000), timestamp, timestamp};
    
    OSStatus status = CMSampleBufferCreateForImageBuffer(kCFAllocatorDefault,
                                                        imageBuffer,
                                                        true,
                                                        NULL,
                                                        NULL,
                                                        _videoFormat,
                                                        &timing,
                                                        &sampleBuffer);
    
    if (status != noErr) {
        LOG_ERROR("Failed to create sample buffer: %d", (int)status);
        return;
    }
    
    if (self.previewLayer.status == AVQueuedSampleBufferRenderingStatusFailed) {
        [self.previewLayer flush];
    }
    
    [self.previewLayer enqueueSampleBuffer:sampleBuffer];
    CFRelease(sampleBuffer);
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
    
    const uint8_t* const parameterSetPointers[2] = { sps, pps };
    const size_t parameterSetSizes[2] = { spsSize, ppsSize };
    
    OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault,
                                                                         2,
                                                                         parameterSetPointers,
                                                                         parameterSetSizes,
                                                                         4,
                                                                         &_videoFormat);
    
    if (status != noErr) {
        LOG_ERROR("Failed to create video format description: %d", (int)status);
        return;
    }
    
    // Setup decompression settings
    CFDictionaryRef attrs = NULL;
    const void *keys[] = { kCVPixelBufferPixelFormatTypeKey };
    uint32_t v = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
    const void *values[] = { CFNumberCreate(NULL, kCFNumberSInt32Type, &v) };
    attrs = CFDictionaryCreate(NULL, keys, values, 1, NULL, NULL);
    
    VTDecompressionOutputCallbackRecord callbackRecord;
    callbackRecord.decompressionOutputCallback = DecompressionSessionDecodeCallback;
    callbackRecord.decompressionOutputRefCon = (__bridge void *)self;
    
    status = VTDecompressionSessionCreate(kCFAllocatorDefault,
                                         _videoFormat,
                                         NULL,
                                         attrs,
                                         &callbackRecord,
                                         &_decompressionSession);
    
    if (status != noErr) {
        LOG_ERROR("Failed to create decompression session: %d", (int)status);
        CFRelease(attrs);
        return;
    }
    
    CFRelease(attrs);
    _hasVideoFormat = YES;
}

- (void)processVideoData:(uint8_t *)data length:(size_t)length timestamp:(uint32_t)timestamp {
    if (!data || length == 0) return;
    
    dispatch_async(_decompressionQueue, ^{
        // Parse NAL units
        uint8_t naluType = data[4] & 0x1F;
        
        if (naluType == 7) { // SPS
            free(_sps);
            _spsSize = length - 4;
            _sps = malloc(_spsSize);
            memcpy(_sps, data + 4, _spsSize);
            
            if (_pps) {
                [self setupDecompressionSessionWithSPS:_sps spsSize:_spsSize
                                                 PPS:_pps ppsSize:_ppsSize];
            }
        }
        else if (naluType == 8) { // PPS
            free(_pps);
            _ppsSize = length - 4;
            _pps = malloc(_ppsSize);
            memcpy(_pps, data + 4, _ppsSize);
            
            if (_sps) {
                [self setupDecompressionSessionWithSPS:_sps spsSize:_spsSize
                                                 PPS:_pps ppsSize:_ppsSize];
            }
        }
        else if (_hasVideoFormat && (naluType == 1 || naluType == 5)) { // IDR or non-IDR
            CMBlockBufferRef blockBuffer = NULL;
            OSStatus status = CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault,
                                                               data,
                                                               length,
                                                               kCFAllocatorNull,
                                                               NULL,
                                                               0,
                                                               length,
                                                               0,
                                                               &blockBuffer);
            
            if (status != noErr) {
                LOG_ERROR("Failed to create block buffer: %d", (int)status);
                return;
            }
            
            CMSampleBufferRef sampleBuffer = NULL;
            const size_t sampleSizesArray[] = {length};
            
            status = CMSampleBufferCreateReady(kCFAllocatorDefault,
                                             blockBuffer,
                                             _videoFormat,
                                             1,
                                             0,
                                             NULL,
                                             1,
                                             sampleSizesArray,
                                             &sampleBuffer);
            
            if (status != noErr) {
                LOG_ERROR("Failed to create sample buffer: %d", (int)status);
                CFRelease(blockBuffer);
                return;
            }
            
            VTDecodeFrameFlags flags = kVTDecodeFrame_EnableAsynchronousDecompression;
            VTDecompressionSessionDecodeFrame(_decompressionSession,
                                            sampleBuffer,
                                            flags,
                                            NULL,
                                            NULL);
            
            CFRelease(sampleBuffer);
            CFRelease(blockBuffer);
        }
    });
}

- (void)processAudioData:(uint8_t *)data length:(size_t)length timestamp:(uint32_t)timestamp {
    // Audio processing implementation
}

- (void)showPreview {
    if (!self.isVisible) {
        self.isVisible = YES;
        UIWindow *window = [UIApplication sharedApplication].keyWindow;
        [window addSubview:self];
    }
}

- (void)hidePreview {
    if (self.isVisible) {
        self.isVisible = NO;
        [self removeFromSuperview];
    }
}

- (void)handlePan:(UIPanGestureRecognizer *)gesture {
    CGPoint translation = [gesture translationInView:self.superview];
    
    if (gesture.state == UIGestureRecognizerStateBegan) {
        self.initialPosition = self.center;
    }
    
    CGPoint newCenter = CGPointMake(self.initialPosition.x + translation.x,
                                   self.initialPosition.y + translation.y);
    
    CGRect bounds = [UIScreen mainScreen].bounds;
    newCenter.x = MAX(self.frame.size.width/2, MIN(newCenter.x, bounds.size.width - self.frame.size.width/2));
    newCenter.y = MAX(self.frame.size.height/2, MIN(newCenter.y, bounds.size.height - self.frame.size.height/2));
    
    self.center = newCenter;
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
}

@end

// C interface implementation
void rtmp_preview_init(void) {
    [[RTMPPreviewView sharedInstance] showPreview];
}

void rtmp_preview_show(void) {
    [[RTMPPreviewView sharedInstance] showPreview];
}

void rtmp_preview_hide(void) {
    [[RTMPPreviewView sharedInstance] hidePreview];
}

void rtmp_preview_process_video(const uint8_t* data, size_t length, uint32_t timestamp) {
    [[RTMPPreviewView sharedInstance] processVideoData:(uint8_t*)data length:length timestamp:timestamp];
}

void rtmp_preview_process_audio(const uint8_t* data, size_t length, uint32_t timestamp) {
    [[RTMPPreviewView sharedInstance] processAudioData:(uint8_t*)data length:length timestamp:timestamp];
}