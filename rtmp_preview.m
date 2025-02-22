#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>
#import "rtmp_preview.h"
#import "rtmp_stream.h"
#import "rtmp_utils.h"

// Configurações otimizadas
static const NSInteger kMaxBufferFrames = 3;
static const NSTimeInterval kStatsUpdateInterval = 0.5;
static const float kMinimumFPS = 25.0f;

@interface RTMPPreviewView () {
    CALayer *_previewLayer;
    dispatch_queue_t _renderQueue;
    dispatch_semaphore_t _frameBufferSemaphore;
    CMFormatDescriptionRef _formatDescription;
    VTDecompressionSessionRef _decompressionSession;
    CFMutableArrayRef _frameBuffer;
    bool _isSetup;
}

@property (nonatomic, strong) UIView *statsView;
@property (nonatomic, strong) UILabel *fpsLabel;
@property (nonatomic, strong) UILabel *qualityLabel;
@property (nonatomic, strong) UILabel *latencyLabel;
@property (nonatomic, assign) NSInteger frameCount;
@property (nonatomic, assign) NSTimeInterval lastFPSUpdate;
@property (nonatomic, assign) NSTimeInterval lastFrameTime;
@property (nonatomic, assign) float currentFPS;
@property (nonatomic, assign) float averageLatency;

@end

@implementation RTMPPreviewView

- (void)dealloc {
    if (_decompressionSession) {
        VTDecompressionSessionInvalidate(_decompressionSession);
        CFRelease(_decompressionSession);
    }
    
    if (_formatDescription) {
        CFRelease(_formatDescription);
    }
    
    if (_frameBuffer) {
        CFRelease(_frameBuffer);
    }
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        [self setupPreviewLayer];
        [self setupRenderQueue];
        [self setupFrameBuffer];
        [self setupStatsView];
        _isSetup = false;
        _averageLatency = 0;
    }
    return self;
}

- (void)setupPreviewLayer {
    _previewLayer = [CALayer layer];
    _previewLayer.frame = self.bounds;
    _previewLayer.contentsGravity = kCAGravityResizeAspect;
    _previewLayer.backgroundColor = [UIColor blackColor].CGColor;
    [self.layer addSublayer:_previewLayer];
}

- (void)setupRenderQueue {
    _renderQueue = dispatch_queue_create("com.rtmpcamera.preview.render", 
        DISPATCH_QUEUE_SERIAL_WITH_AUTORELEASE_POOL);
    dispatch_set_target_queue(_renderQueue, 
        dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
    _frameBufferSemaphore = dispatch_semaphore_create(kMaxBufferFrames);
}

- (void)setupFrameBuffer {
    _frameBuffer = CFArrayCreateMutable(kCFAllocatorDefault, kMaxBufferFrames, NULL);
}

- (void)setupStatsView {
    self.statsView = [[UIView alloc] initWithFrame:CGRectMake(10, 10, 200, 90)];
    self.statsView.backgroundColor = [UIColor colorWithWhite:0 alpha:0.7];
    self.statsView.layer.cornerRadius = 5;
    
    self.fpsLabel = [[UILabel alloc] initWithFrame:CGRectMake(5, 5, 190, 20)];
    self.fpsLabel.textColor = [UIColor whiteColor];
    self.fpsLabel.font = [UIFont systemFontOfSize:12];
    
    self.qualityLabel = [[UILabel alloc] initWithFrame:CGRectMake(5, 30, 190, 20)];
    self.qualityLabel.textColor = [UIColor whiteColor];
    self.qualityLabel.font = [UIFont systemFontOfSize:12];
    
    self.latencyLabel = [[UILabel alloc] initWithFrame:CGRectMake(5, 55, 190, 20)];
    self.latencyLabel.textColor = [UIColor whiteColor];
    self.latencyLabel.font = [UIFont systemFontOfSize:12];
    
    [self.statsView addSubview:self.fpsLabel];
    [self.statsView addSubview:self.qualityLabel];
    [self.statsView addSubview:self.latencyLabel];
    [self addSubview:self.statsView];
}

- (void)setupDecompressionSession {
    if (_isSetup) return;
    
    // Configuração otimizada para decodificação por hardware
    NSDictionary *attributes = @{
        (NSString*)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        (NSString*)kCVPixelBufferIOSurfacePropertiesKey: @{},
        (NSString*)kCVPixelBufferMetalCompatibilityKey: @YES,
    };
    
    VTDecompressionOutputCallbackRecord callback;
    callback.decompressionOutputCallback = decompressionOutputCallback;
    callback.decompressionOutputRefCon = (__bridge void *)self;
    
    // Cria sessão de decodificação com prioridade alta
    NSDictionary *videoDecoderSpecification = @{
        (NSString*)kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder: @YES,
        (NSString*)kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder: @YES
    };
    
    OSStatus status = VTDecompressionSessionCreate(
        kCFAllocatorDefault,
        _formatDescription,
        (__bridge CFDictionaryRef)videoDecoderSpecification,
        (__bridge CFDictionaryRef)attributes,
        &callback,
        &_decompressionSession
    );
    
    if (status != noErr) {
        NSLog(@"Erro ao criar sessão de decodificação: %d", (int)status);
        return;
    }
    
    // Configura prioridade real-time
    VTSessionSetProperty(_decompressionSession,
        kVTDecompressionPropertyKey_RealTime,
        kCFBooleanTrue);
    
    _isSetup = true;
}

static void decompressionOutputCallback(void *decompressionOutputRefCon,
                                      void *sourceFrameRefCon,
                                      OSStatus status,
                                      VTDecodeInfoFlags infoFlags,
                                      CVImageBufferRef imageBuffer,
                                      CMTime presentationTimeStamp,
                                      CMTime presentationDuration) {
    RTMPPreviewView *preview = (__bridge RTMPPreviewView *)decompressionOutputRefCon;
    [preview displayDecodedFrame:imageBuffer withTimestamp:presentationTimeStamp];
}

- (void)displayDecodedFrame:(CVImageBufferRef)imageBuffer withTimestamp:(CMTime)timestamp {
    if (!imageBuffer) return;
    
    // Calcula latência
    NSTimeInterval currentTime = CACurrentMediaTime();
    NSTimeInterval frameLatency = currentTime - CMTimeGetSeconds(timestamp);
    self.averageLatency = (self.averageLatency * 0.7) + (frameLatency * 0.3);
    
    dispatch_async(dispatch_get_main_queue(), ^{
        self->_previewLayer.contents = (__bridge id)imageBuffer;
        self.frameCount++;
        [self updateStats];
    });
}

- (void)updateStats {
    NSTimeInterval now = CACurrentMediaTime();
    if (now - self.lastFPSUpdate >= kStatsUpdateInterval) {
        float fps = self.frameCount / (now - self.lastFPSUpdate);
        self.currentFPS = fps;
        
        self.fpsLabel.text = [NSString stringWithFormat:@"FPS: %.1f", fps];
        self.latencyLabel.text = [NSString stringWithFormat:@"Latência: %.1f ms", self.averageLatency * 1000];
        
        if (self.stream) {
            stream_stats_t stats = rtmp_stream_get_stats(self.stream);
            NSString *quality;
            UIColor *qualityColor;
            
            if (stats.buffer_usage > 0.8) {
                quality = @"Ruim";
                qualityColor = [UIColor redColor];
            } else if (stats.buffer_usage > 0.5) {
                quality = @"Média";
                qualityColor = [UIColor yellowColor];
            } else {
                quality = @"Ótima";
                qualityColor = [UIColor greenColor];
            }
            
            self.qualityLabel.text = [NSString stringWithFormat:@"Qualidade: %@", quality];
            self.qualityLabel.textColor = qualityColor;
        }
        
        // Alerta se FPS está baixo
        if (fps < kMinimumFPS) {
            NSLog(@"Alerta: FPS baixo (%.1f)", fps);
        }
        
        self.frameCount = 0;
        self.lastFPSUpdate = now;
    }
}

- (void)processVideoFrame:(video_frame_t *)frame {
    if (!frame || !frame->data) return;
    
    if (!_isSetup) {
        [self setupDecompressionSession];
    }
    
    // Evita buffer overflow
    dispatch_semaphore_wait(_frameBufferSemaphore, DISPATCH_TIME_FOREVER);
    
    dispatch_async(_renderQueue, ^{
        CMBlockBufferRef blockBuffer = NULL;
        OSStatus status = CMBlockBufferCreateWithMemoryBlock(
            kCFAllocatorDefault,
            frame->data,
            frame->length,
            kCFAllocatorNull,
            NULL,
            0,
            frame->length,
            0,
            &blockBuffer
        );
        
        if (status != noErr) {
            dispatch_semaphore_signal(self->_frameBufferSemaphore);
            return;
        }
        
        CMSampleBufferRef sampleBuffer = NULL;
        const size_t sampleSizeArray[] = {frame->length};
        status = CMSampleBufferCreateReady(
            kCFAllocatorDefault,
            blockBuffer,
            self->_formatDescription,
            1,
            0,
            NULL,
            1,
            sampleSizeArray,
            &sampleBuffer
        );
        
        if (status != noErr) {
            CFRelease(blockBuffer);
            dispatch_semaphore_signal(self->_frameBufferSemaphore);
            return;
        }
        
        VTDecodeFrameFlags flags = kVTDecodeFrame_EnableAsynchronousDecompression | 
                                  kVTDecodeFrame_1xRealTimePlayback;
        
        VTDecompressionSessionDecodeFrame(
            self->_decompressionSession,
            sampleBuffer,
            flags,
            NULL,
            NULL
        );
        
        CFRelease(sampleBuffer);
        CFRelease(blockBuffer);
        dispatch_semaphore_signal(self->_frameBufferSemaphore);
    });
}

- (void)layoutSubviews {
    [super layoutSubviews];
    _previewLayer.frame = self.bounds;
}

@end