#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import "rtmp_preview.h"
#import "rtmp_core.h"

// Configurações de buffer
#define MAX_FRAME_BUFFER_SIZE 8
#define MAX_FRAME_DELAY 0.1 // 100ms
#define MEMORY_BUFFER_SIZE 1024 * 1024 * 10 // 10MB

@interface RTMPPreviewManager () {
    dispatch_queue_t _processingQueue;
    dispatch_queue_t _renderQueue;
    dispatch_semaphore_t _frameSync;
    CMMemoryPoolRef _memoryPool;
    NSMutableArray<AVSampleBufferDisplayLayer *> *_displayLayers;
    volatile BOOL _isProcessing;
    
    // Buffer circular para frames
    struct {
        CMSampleBufferRef buffers[MAX_FRAME_BUFFER_SIZE];
        uint32_t head;
        uint32_t tail;
        uint32_t count;
    } _frameBuffer;
}

@property (nonatomic, strong) NSLock *bufferLock;
@property (nonatomic, strong) dispatch_source_t frameTimer;
@property (nonatomic, assign) CMVideoFormatDescriptionRef formatDescription;
@property (nonatomic, assign) VTDecompressionSessionRef decompressionSession;

@end

@implementation RTMPPreviewManager

+ (instancetype)sharedInstance {
    static RTMPPreviewManager *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[self alloc] init];
    });
    return instance;
}

- (instancetype)init {
    if (self = [super init]) {
        _processingQueue = dispatch_queue_create("com.rtmpcamera.preview.processing", DISPATCH_QUEUE_SERIAL);
        _renderQueue = dispatch_queue_create("com.rtmpcamera.preview.render", DISPATCH_QUEUE_SERIAL);
        _frameSync = dispatch_semaphore_create(1);
        _bufferLock = [[NSLock alloc] init];
        _displayLayers = [NSMutableArray array];
        _memoryPool = CMMemoryPoolCreate(NULL);
        
        [self setupDecompressionSession];
        [self setupFrameTimer];
    }
    return self;
}

- (void)setupDecompressionSession {
    // Configuração do formato de vídeo
    CMVideoFormatDescriptionCreate(kCFAllocatorDefault,
                                 kCMVideoCodecType_H264,
                                 1920, 1080,
                                 NULL,
                                 &_formatDescription);
    
    // Configuração do VTDecompressionSession
    VTDecompressionOutputCallbackRecord callback;
    callback.decompressionOutputCallback = decompressionOutputCallback;
    callback.decompressionOutputRefCon = (__bridge void *)self;
    
    VTDecompressionSessionCreate(kCFAllocatorDefault,
                                _formatDescription,
                                NULL,
                                NULL,
                                &callback,
                                &_decompressionSession);
}

- (void)setupFrameTimer {
    _frameTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, _renderQueue);
    dispatch_source_set_timer(_frameTimer,
                            dispatch_time(DISPATCH_TIME_NOW, 0),
                            NSEC_PER_SEC / 60, // 60 FPS
                            NSEC_PER_MSEC);
    
    __weak typeof(self) weakSelf = self;
    dispatch_source_set_event_handler(_frameTimer, ^{
        [weakSelf renderNextFrame];
    });
}

- (void)addPreviewLayer:(AVSampleBufferDisplayLayer *)layer {
    [_displayLayers addObject:layer];
    layer.videoGravity = AVLayerVideoGravityResizeAspectFill;
    
    // Configurações para baixa latência
    layer.preferredFrameRate = 60;
    layer.shouldRasterize = NO;
}

- (void)startProcessing {
    if (_isProcessing) return;
    
    _isProcessing = YES;
    dispatch_resume(_frameTimer);
}

- (void)stopProcessing {
    if (!_isProcessing) return;
    
    _isProcessing = NO;
    dispatch_suspend(_frameTimer);
    [self clearBuffers];
}

- (void)processVideoFrame:(uint8_t *)data length:(size_t)length timestamp:(uint64_t)timestamp {
    if (!_isProcessing) return;
    
    dispatch_async(_processingQueue, ^{
        @autoreleasepool {
            CMBlockBufferRef blockBuffer = NULL;
            CMSampleBufferRef sampleBuffer = NULL;
            
            // Cria block buffer
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
            
            if (status == noErr) {
                // Cria sample buffer
                const size_t sampleSizes[] = {length};
                status = CMSampleBufferCreate(
                    kCFAllocatorDefault,
                    blockBuffer,
                    TRUE,
                    NULL,
                    NULL,
                    self.formatDescription,
                    1,
                    0,
                    NULL,
                    1,
                    sampleSizes,
                    &sampleBuffer
                );
                
                if (status == noErr) {
                    // Decodifica frame
                    VTDecodeFrameFlags flags = kVTDecodeFrame_EnableAsynchronousDecompression;
                    VTDecompressionSessionDecodeFrame(
                        self.decompressionSession,
                        sampleBuffer,
                        flags,
                        NULL,
                        NULL
                    );
                }
            }
            
            if (blockBuffer) CFRelease(blockBuffer);
            if (sampleBuffer) CFRelease(sampleBuffer);
        }
    });
}

- (void)renderNextFrame {
    [_bufferLock lock];
    
    if (_frameBuffer.count > 0) {
        CMSampleBufferRef frame = _frameBuffer.buffers[_frameBuffer.tail];
        _frameBuffer.buffers[_frameBuffer.tail] = NULL;
        _frameBuffer.tail = (_frameBuffer.tail + 1) % MAX_FRAME_BUFFER_SIZE;
        _frameBuffer.count--;
        
        [_bufferLock unlock];
        
        if (frame) {
            // Renderiza frame em todas as layers
            for (AVSampleBufferDisplayLayer *layer in _displayLayers) {
                if (layer.status != AVQueuedSampleBufferRenderingStatusFailed) {
                    [layer enqueueSampleBuffer:frame];
                }
            }
            
            CFRelease(frame);
        }
    } else {
        [_bufferLock unlock];
    }
}

static void decompressionOutputCallback(void *decompressionOutputRefCon,
                                      void *sourceFrameRefCon,
                                      OSStatus status,
                                      VTDecodeInfoFlags infoFlags,
                                      CVImageBufferRef imageBuffer,
                                      CMTime presentationTimeStamp,
                                      CMTime presentationDuration) {
    if (status != noErr || !imageBuffer) return;
    
    RTMPPreviewManager *self = (__bridge RTMPPreviewManager *)decompressionOutputRefCon;
    
    // Cria sample buffer para o frame decodificado
    CMSampleBufferRef sampleBuffer = NULL;
    CMSampleTimingInfo timing = {presentationDuration, presentationTimeStamp, kCMTimeInvalid};
    
    CMSampleBufferCreateForImageBuffer(
        kCFAllocatorDefault,
        imageBuffer,
        true,
        NULL,
        NULL,
        self.formatDescription,
        &timing,
        &sampleBuffer
    );
    
    if (sampleBuffer) {
        [self enqueueSampleBuffer:sampleBuffer];
        CFRelease(sampleBuffer);
    }
}

- (void)enqueueSampleBuffer:(CMSampleBufferRef)sampleBuffer {
    [_bufferLock lock];
    
    if (_frameBuffer.count < MAX_FRAME_BUFFER_SIZE) {
        CFRetain(sampleBuffer);
        _frameBuffer.buffers[_frameBuffer.head] = sampleBuffer;
        _frameBuffer.head = (_frameBuffer.head + 1) % MAX_FRAME_BUFFER_SIZE;
        _frameBuffer.count++;
    }
    
    [_bufferLock unlock];
}

- (void)clearBuffers {
    [_bufferLock lock];
    
    for (uint32_t i = 0; i < MAX_FRAME_BUFFER_SIZE; i++) {
        if (_frameBuffer.buffers[i]) {
            CFRelease(_frameBuffer.buffers[i]);
            _frameBuffer.buffers[i] = NULL;
        }
    }
    
    _frameBuffer.head = 0;
    _frameBuffer.tail = 0;
    _frameBuffer.count = 0;
    
    [_bufferLock unlock];
}

- (void)handleLowMemory {
    // Limpa buffers não essenciais
    [self clearBuffers];
    
    // Recria pool de memória
    if (_memoryPool) {
        CMMemoryPoolInvalidate(_memoryPool);
        CFRelease(_memoryPool);
        _memoryPool = CMMemoryPoolCreate(NULL);
    }
}

- (void)dealloc {
    [self stopProcessing];
    
    if (_frameTimer) {
        dispatch_source_cancel(_frameTimer);
    }
    
    if (_decompressionSession) {
        VTDecompressionSessionInvalidate(_decompressionSession);
        CFRelease(_decompressionSession);
    }
    
    if (_formatDescription) {
        CFRelease(_formatDescription);
    }
    
    if (_memoryPool) {
        CMMemoryPoolInvalidate(_memoryPool);
        CFRelease(_memoryPool);
    }
}

@end