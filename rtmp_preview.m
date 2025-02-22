#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import "rtmp_preview.h"

// Configurações para otimização de performance
#define MAX_FRAME_BUFFER_SIZE 8   // Buffer de 8 frames
#define TARGET_FRAME_DURATION 1.0/30.0  // 30 fps
#define SYNC_THRESHOLD 0.1  // 100ms threshold para sincronização
#define MEMORY_POOL_SIZE (1024 * 1024 * 10)  // 10MB pool

@interface RTMPPreviewManager () {
    dispatch_queue_t _processingQueue;
    dispatch_queue_t _renderQueue;
    dispatch_semaphore_t _frameSync;
    CMMemoryPoolRef _memoryPool;
    
    struct {
        CMSampleBufferRef buffers[MAX_FRAME_BUFFER_SIZE];
        uint32_t head;
        uint32_t tail;
        uint32_t count;
        uint64_t lastTimestamp;
        pthread_mutex_t mutex;
    } _frameBuffer;
    
    struct {
        uint32_t droppedFrames;
        uint32_t processedFrames;
        double averageLatency;
        uint32_t bufferUnderruns;
    } _stats;
}

@property (nonatomic, strong) AVSampleBufferDisplayLayer *previewLayer;
@property (nonatomic, strong) dispatch_source_t frameTimer;
@property (nonatomic, assign) CMVideoFormatDescriptionRef formatDescription;
@property (nonatomic, assign) VTDecompressionSessionRef decompressionSession;
@property (nonatomic, strong) NSMutableArray<AVSampleBufferDisplayLayer *> *displayLayers;
@property (nonatomic, assign) BOOL isProcessing;
@property (nonatomic, assign) CMTime lastPresentationTime;

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
        // Inicializar queues
        _processingQueue = dispatch_queue_create("com.rtmpcamera.preview.processing", 
                                               DISPATCH_QUEUE_SERIAL);
        _renderQueue = dispatch_queue_create("com.rtmpcamera.preview.render", 
                                           DISPATCH_QUEUE_SERIAL);
        
        // Inicializar sincronização
        _frameSync = dispatch_semaphore_create(1);
        pthread_mutex_init(&_frameBuffer.mutex, NULL);
        
        // Criar pool de memória
        _memoryPool = CMMemoryPoolCreate(NULL);
        
        // Configurar camadas de display
        _displayLayers = [NSMutableArray array];
        
        // Configurar decomposição de vídeo
        [self setupDecompressionSession];
        
        // Configurar timer de renderização
        [self setupFrameTimer];
    }
    return self;
}

- (void)setupDecompressionSession {
    // Configurar formato de vídeo
    CMVideoFormatDescriptionCreate(kCFAllocatorDefault,
                                 kCMVideoCodecType_H264,
                                 1920, 1080,
                                 NULL,
                                 &_formatDescription);
    
    // Configurar callback de decomposição
    VTDecompressionOutputCallbackRecord callback;
    callback.decompressionOutputCallback = decompressionOutputCallback;
    callback.decompressionOutputRefCon = (__bridge void *)self;
    
    // Criar sessão de decomposição
    NSDictionary *attributes = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        (id)kCVPixelBufferMetalCompatibilityKey: @YES,
        (id)kCVPixelBufferWidthKey: @1920,
        (id)kCVPixelBufferHeightKey: @1080
    };
    
    VTDecompressionSessionCreate(kCFAllocatorDefault,
                                _formatDescription,
                                NULL,
                                (__bridge CFDictionaryRef)attributes,
                                &callback,
                                &_decompressionSession);
    
    // Configurar para baixa latência
    VTSessionSetProperty(_decompressionSession,
                        kVTDecompressionPropertyKey_RealTime,
                        kCFBooleanTrue);
}

- (void)setupFrameTimer {
    self.frameTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, _renderQueue);
    
    dispatch_source_set_timer(self.frameTimer,
                            dispatch_time(DISPATCH_TIME_NOW, 0),
                            (int64_t)(TARGET_FRAME_DURATION * NSEC_PER_SEC),
                            (int64_t)(1 * NSEC_PER_MSEC));
    
    __weak typeof(self) weakSelf = self;
    dispatch_source_set_event_handler(self.frameTimer, ^{
        [weakSelf renderNextFrame];
    });
}

- (void)addPreviewLayer:(AVSampleBufferDisplayLayer *)layer {
    layer.videoGravity = AVLayerVideoGravityResizeAspectFill;
    
    // Otimizações para baixa latência
    layer.preferredFrameRate = 30;
    layer.shouldRasterize = NO;
    
    [self.displayLayers addObject:layer];
}

- (void)startProcessing {
    if (self.isProcessing) return;
    
    self.isProcessing = YES;
    dispatch_resume(self.frameTimer);
    
    // Resetar estatísticas
    memset(&_stats, 0, sizeof(_stats));
}

- (void)stopProcessing {
    if (!self.isProcessing) return;
    
    self.isProcessing = NO;
    dispatch_suspend(self.frameTimer);
    
    // Limpar buffers
    [self clearBuffers];
}

- (void)processVideoFrame:(uint8_t *)data length:(size_t)length timestamp:(uint64_t)timestamp {
    if (!self.isProcessing) return;
    
    dispatch_async(_processingQueue, ^{
        @autoreleasepool {
            // Verificar timing
            if (_frameBuffer.lastTimestamp > 0) {
                double delta = (timestamp - _frameBuffer.lastTimestamp) / 1000.0;
                if (delta < TARGET_FRAME_DURATION * 0.5) {
                    _stats.droppedFrames++;
                    return;
                }
            }
            
            // Criar block buffer
            CMBlockBufferRef blockBuffer = NULL;
            OSStatus status = CMBlockBufferCreateWithMemoryBlock(
                kCFAllocatorDefault,
                data,
                length,
                kCFAllocatorDefault,
                NULL,
                0,
                length,
                0,
                &blockBuffer
            );
            
            if (status != noErr) return;
            
            // Criar sample buffer
            CMSampleBufferRef sampleBuffer = NULL;
            const size_t sampleSizes[] = {length};
            
            status = CMSampleBufferCreateReady(
                kCFAllocatorDefault,
                blockBuffer,
                self.formatDescription,
                1,
                0,
                NULL,
                1,
                sampleSizes,
                &sampleBuffer
            );
            
            CFRelease(blockBuffer);
            
            if (status != noErr) return;
            
            // Configurar timing
            CMTime presentationTime = CMTimeMake(timestamp, 1000);
            CFDictionaryRef empty = CFDictionaryCreate(kCFAllocatorDefault, NULL, NULL, 0,
                &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CFArrayRef attachments = CFArrayCreate(kCFAllocatorDefault, (const void **)&empty, 1,
                &kCFTypeArrayCallBacks);
            CFRelease(empty);
            
            CMSampleBufferSetOutputPresentationTimeStamp(sampleBuffer, presentationTime);
            CMSampleBufferSetAttachments(sampleBuffer, attachments);
            CFRelease(attachments);
            
            // Decodificar frame
            VTDecodeFrameFlags flags = kVTDecodeFrame_EnableAsynchronousDecompression;
            VTDecompressionSessionDecodeFrame(
                self.decompressionSession,
                sampleBuffer,
                flags,
                NULL,
                NULL
            );
            
            CFRelease(sampleBuffer);
            
            _frameBuffer.lastTimestamp = timestamp;
            _stats.processedFrames++;
        }
    });
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
    
    // Criar sample buffer para o frame decodificado
    CMSampleTimingInfo timing = {
        .duration = presentationDuration,
        .presentationTimeStamp = presentationTimeStamp,
        .decodeTimeStamp = kCMTimeInvalid
    };
    
    CMSampleBufferRef sampleBuffer;
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
    
    [self enqueueSampleBuffer:sampleBuffer];
    CFRelease(sampleBuffer);
}

- (void)enqueueSampleBuffer:(CMSampleBufferRef)sampleBuffer {
    if (!self.isProcessing) return;
    
    pthread_mutex_lock(&_frameBuffer.mutex);
    
    if (_frameBuffer.count < MAX_FRAME_BUFFER_SIZE) {
        CFRetain(sampleBuffer);
        _frameBuffer.buffers[_frameBuffer.head] = sampleBuffer;
        _frameBuffer.head = (_frameBuffer.head + 1) % MAX_FRAME_BUFFER_SIZE;
        _frameBuffer.count++;
    } else {
        _stats.droppedFrames++;
    }
    
    pthread_mutex_unlock(&_frameBuffer.mutex);
}

- (void)renderNextFrame {
    pthread_mutex_lock(&_frameBuffer.mutex);
    
    if (_frameBuffer.count > 0) {
        CMSampleBufferRef frame = _frameBuffer.buffers[_frameBuffer.tail];
        _frameBuffer.buffers[_frameBuffer.tail] = NULL;
        _frameBuffer.tail = (_frameBuffer.tail + 1) % MAX_FRAME_BUFFER_SIZE;
        _frameBuffer.count--;
        
        pthread_mutex_unlock(&_frameBuffer.mutex);
        
        if (frame) {
            // Renderizar frame em todas as layers
            for (AVSampleBufferDisplayLayer *layer in self.displayLayers) {
                if (layer.status != AVQueuedSampleBufferRenderingStatusFailed) {
                    [layer enqueueSampleBuffer:frame];
                }
            }
            
            CFRelease(frame);
        }
    } else {
        _stats.bufferUnderruns++;
        pthread_mutex_unlock(&_frameBuffer.mutex);
    }
}

- (void)clearBuffers {
    pthread_mutex_lock(&_frameBuffer.mutex);
    
    for (uint32_t i = 0; i < MAX_FRAME_BUFFER_SIZE; i++) {
        if (_frameBuffer.buffers[i]) {
            CFRelease(_frameBuffer.buffers[i]);
            _frameBuffer.buffers[i] = NULL;
        }
    }
    
    _frameBuffer.head = 0;
    _frameBuffer.tail = 0;
    _frameBuffer.count = 0;
    _frameBuffer.lastTimestamp = 0;
    
    pthread_mutex_unlock(&_frameBuffer.mutex);
}

- (void)getStats:(RTMPPreviewStats *)stats {
    if (!stats) return;
    
    stats->processedFrames = _stats.processedFrames;
    stats->droppedFrames = _stats.droppedFrames;
    stats->bufferUnderruns = _stats.bufferUnderruns;
    stats->averageLatency = _stats.averageLatency;
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
    
    pthread_mutex_destroy(&_frameBuffer.mutex);
}

@end