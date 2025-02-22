#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#import "rtmp_core.h"

@interface RTMPPreviewManager : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>

@property (nonatomic, strong) AVSampleBufferDisplayLayer *previewLayer;
@property (nonatomic, strong) dispatch_queue_t processingQueue;
@property (nonatomic, strong) CMMemoryPool *memoryPool;
@property (nonatomic, strong) NSMutableArray *frameBuffer;
@property (nonatomic, assign) CMVideoFormatDescriptionRef formatDescription;
@property (nonatomic, assign) BOOL isProcessing;

+ (instancetype)sharedInstance;
- (void)setupPreviewInView:(UIView *)view;
- (void)startProcessing;
- (void)stopProcessing;
- (void)processVideoFrame:(uint8_t *)data length:(size_t)length timestamp:(uint32_t)timestamp;
- (void)processAudioFrame:(uint8_t *)data length:(size_t)length timestamp:(uint32_t)timestamp;

@end

@implementation RTMPPreviewManager {
    CMSimpleQueueRef _previewQueue;
    dispatch_semaphore_t _frameSemaphore;
}

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
        _processingQueue = dispatch_queue_create("com.rtmpcamera.preview", DISPATCH_QUEUE_SERIAL);
        _frameBuffer = [NSMutableArray array];
        _frameSemaphore = dispatch_semaphore_create(1);
        _memoryPool = [[CMMemoryPool alloc] init];
        
        // Cria fila de preview com capacidade para 8 frames
        CMSimpleQueueCreate(kCFAllocatorDefault, 8, &_previewQueue);
        
        // Configura callbacks do RTMP
        rtmp_server_set_callbacks(
            ^(uint8_t *data, size_t length, uint32_t timestamp) {
                [self processVideoFrame:data length:length timestamp:timestamp];
            },
            ^(uint8_t *data, size_t length, uint32_t timestamp) {
                [self processAudioFrame:data length:length timestamp:timestamp];
            }
        );
    }
    return self;
}

- (void)setupPreviewInView:(UIView *)view {
    self.previewLayer = [[AVSampleBufferDisplayLayer alloc] init];
    self.previewLayer.frame = view.bounds;
    self.previewLayer.videoGravity = AVLayerVideoGravityResizeAspectFill;
    self.previewLayer.opaque = YES;
    
    // Configurações para baixa latência
    self.previewLayer.preferredFrameRate = 30;
    self.previewLayer.shouldRasterize = NO;
    
    [view.layer addSublayer:self.previewLayer];
}

- (void)startProcessing {
    self.isProcessing = YES;
    
    // Inicia loop de processamento
    dispatch_async(self.processingQueue, ^{
        [self processFrameLoop];
    });
}

- (void)stopProcessing {
    self.isProcessing = NO;
    [self.previewLayer flushAndRemoveImage];
    CMSimpleQueueReset(_previewQueue);
}

- (void)processFrameLoop {
    while (self.isProcessing) {
        @autoreleasepool {
            dispatch_semaphore_wait(_frameSemaphore, DISPATCH_TIME_FOREVER);
            
            if (self.frameBuffer.count > 0) {
                CMSampleBufferRef sampleBuffer = (__bridge CMSampleBufferRef)self.frameBuffer[0];
                [self.frameBuffer removeObjectAtIndex:0];
                
                if (self.previewLayer.status != AVQueuedSampleBufferRenderingStatusFailed) {
                    [self.previewLayer enqueueSampleBuffer:sampleBuffer];
                }
                
                CFRelease(sampleBuffer);
            }
            
            dispatch_semaphore_signal(_frameSemaphore);
        }
        
        // Pequeno delay para não sobrecarregar
        usleep(1000); // 1ms
    }
}

- (void)processVideoFrame:(uint8_t *)data length:(size_t)length timestamp:(uint32_t)timestamp {
    if (!self.isProcessing) return;
    
    @autoreleasepool {
        // Criar formato de vídeo se necessário
        if (!self.formatDescription) {
            [self createVideoFormatDescription];
        }
        
        // Criar CMBlockBuffer
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
        
        if (status != noErr) {
            NSLog(@"Error creating block buffer: %d", (int)status);
            return;
        }
        
        // Criar CMSampleBuffer
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
        
        if (status != noErr) {
            NSLog(@"Error creating sample buffer: %d", (int)status);
            return;
        }
        
        // Definir timestamp
        CMTime presentationTime = CMTimeMake(timestamp, 1000);
        CFDictionaryRef empty = CFDictionaryCreate(kCFAllocatorDefault, NULL, NULL, 0, 
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFArrayRef attachments = CFArrayCreate(kCFAllocatorDefault, (const void **)&empty, 1, 
            &kCFTypeArrayCallBacks);
        CFRelease(empty);
        
        status = CMSampleBufferSetOutputPresentationTimeStamp(sampleBuffer, presentationTime);
        status = CMSampleBufferSetAttachments(sampleBuffer, attachments);
        CFRelease(attachments);
        
        // Adicionar ao buffer
        dispatch_semaphore_wait(_frameSemaphore, DISPATCH_TIME_FOREVER);
        [self.frameBuffer addObject:(__bridge id)sampleBuffer];
        dispatch_semaphore_signal(_frameSemaphore);
    }
}

- (void)processAudioFrame:(uint8_t *)data length:(size_t)length timestamp:(uint32_t)timestamp {
    // Implementar processamento de áudio se necessário
}

- (void)createVideoFormatDescription {
    CMVideoFormatDescriptionCreate(
        kCFAllocatorDefault,
        kCMVideoCodecType_H264,
        1920,  // width
        1080,  // height
        NULL,  // extensions
        &_formatDescription
    );
}

- (void)dealloc {
    [self stopProcessing];
    
    if (_previewQueue) {
        CMSimpleQueueRelease(_previewQueue);
    }
    
    if (_formatDescription) {
        CFRelease(_formatDescription);
    }
}

@end