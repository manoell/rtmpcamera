#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import "rtmp_core.h"
#import "substrate.h"

// RTMP Server Configuration
#define RTMP_SERVER_PORT 1935
#define RTMP_PREVIEW_ENABLED 1

// Original method declaration
static void (*original_captureOutput)(id self, SEL _cmd, AVCaptureOutput *captureOutput, CMSampleBufferRef sampleBuffer, AVCaptureConnection *connection);

@interface RTMPCameraManager : NSObject

@property (nonatomic, strong) RTMPPreviewController *previewController;
@property (nonatomic, assign) rtmp_server_t *rtmpServer;
@property (nonatomic, assign) BOOL isActive;
@property (nonatomic, strong) NSString *streamKey;
@property (nonatomic, strong) dispatch_queue_t serverQueue;
@property (nonatomic, strong) NSString *currentStreamName;
@property (nonatomic, assign) BOOL isPublishing;

+ (instancetype)sharedManager;
- (void)startServer;
- (void)stopServer;
- (void)setupPreviewWithView:(UIView *)containerView;
- (void)injectVideoFrame:(CMSampleBufferRef)sampleBuffer;
- (void)startPublishingWithName:(NSString *)streamName;
- (void)stopPublishing;

@end

@implementation RTMPCameraManager

+ (instancetype)sharedManager {
    static RTMPCameraManager *sharedManager = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        sharedManager = [[self alloc] init];
    });
    return sharedManager;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _serverQueue = dispatch_queue_create("com.rtmpcamera.server", DISPATCH_QUEUE_SERIAL);
        _isActive = NO;
        _streamKey = @"live";
        _previewController = [[RTMPPreviewController alloc] init];
        _isPublishing = NO;
    }
    return self;
}

- (void)startServer {
    if (self.isActive) return;
    
    dispatch_async(self.serverQueue, ^{
        // Initialize RTMP server
        self.rtmpServer = rtmp_server_create();
        if (!self.rtmpServer) {
            NSLog(@"[RTMPCamera] Failed to create RTMP server");
            return;
        }
        
        // Configure server
        rtmp_server_config_t config = {
            .port = RTMP_SERVER_PORT,
            .chunk_size = 4096,
            .window_size = 2500000,
            .peer_bandwidth = 2500000
        };
        
        if (rtmp_server_configure(self.rtmpServer, &config) < 0) {
            NSLog(@"[RTMPCamera] Failed to configure RTMP server");
            rtmp_server_destroy(self.rtmpServer);
            self.rtmpServer = NULL;
            return;
        }
        
        // Set server callbacks
        rtmp_server_set_callbacks(self.rtmpServer,
                                ^(rtmp_server_t *server, rtmp_session_t *session) {
            NSLog(@"[RTMPCamera] Client connected");
        },
                                ^(rtmp_server_t *server, rtmp_session_t *session) {
            NSLog(@"[RTMPCamera] Client disconnected");
        },
                                ^(rtmp_server_t *server, rtmp_session_t *session, const char *stream_name) {
            NSLog(@"[RTMPCamera] Stream published: %s", stream_name);
            self.currentStreamName = [NSString stringWithUTF8String:stream_name];
            self.isPublishing = YES;
        },
                                ^(rtmp_server_t *server, rtmp_session_t *session, const char *stream_name) {
            NSLog(@"[RTMPCamera] Stream played: %s", stream_name);
        });
        
        // Start server
        if (rtmp_server_start(self.rtmpServer) < 0) {
            NSLog(@"[RTMPCamera] Failed to start RTMP server");
            rtmp_server_destroy(self.rtmpServer);
            self.rtmpServer = NULL;
            return;
        }
        
        self.isActive = YES;
        NSLog(@"[RTMPCamera] RTMP server started on port %d", RTMP_SERVER_PORT);
    });
}

- (void)stopServer {
    if (!self.isActive) return;
    
    dispatch_async(self.serverQueue, ^{
        if (self.rtmpServer) {
            rtmp_server_stop(self.rtmpServer);
            rtmp_server_destroy(self.rtmpServer);
            self.rtmpServer = NULL;
        }
        self.isActive = NO;
        self.isPublishing = NO;
        self.currentStreamName = nil;
        NSLog(@"[RTMPCamera] RTMP server stopped");
    });
}

- (void)setupPreviewWithView:(UIView *)containerView {
#if RTMP_PREVIEW_ENABLED
    if (!containerView) return;
    
    dispatch_async(dispatch_get_main_queue(), ^{
        // Setup preview controller
        self.previewController.view.frame = containerView.bounds;
        self.previewController.view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        [containerView addSubview:self.previewController.view];
        [self.previewController startPreview];
    });
#endif
}

- (void)injectVideoFrame:(CMSampleBufferRef)sampleBuffer {
    if (!self.isActive || !sampleBuffer || !self.isPublishing) return;
    
    @autoreleasepool {
        // Get video format
        CMVideoFormatDescriptionRef formatDescription = CMSampleBufferGetFormatDescription(sampleBuffer);
        if (!formatDescription) return;
        
        // Get dimension
        CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(formatDescription);
        
        // Get raw buffer
        CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
        if (!imageBuffer) return;
        
        CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
        
        @try {
            // Get data pointers
            void *baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);
            size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
            size_t height = CVPixelBufferGetHeight(imageBuffer);
            size_t dataSize = bytesPerRow * height;
            
            // Create copy of frame data
            uint8_t *frameData = malloc(dataSize);
            if (frameData) {
                memcpy(frameData, baseAddress, dataSize);
                
                // Get timestamp
                CMTime presentationTime = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
                uint32_t timestamp = (uint32_t)(CMTimeGetSeconds(presentationTime) * 1000);
                
                // Send frame to RTMP server
                dispatch_async(self.serverQueue, ^{
                    if (self.rtmpServer && self.isActive && self.isPublishing) {
                        rtmp_server_send_video(self.rtmpServer,
                                             frameData,
                                             dataSize,
                                             dimensions.width,
                                             dimensions.height,
                                             timestamp);
                    }
                    free(frameData);
                });
                
#if RTMP_PREVIEW_ENABLED
                // Update preview if enabled
                uint8_t *previewData = malloc(dataSize);
                if (previewData) {
                    memcpy(previewData, baseAddress, dataSize);
                    [self.previewController processVideoData:previewData 
                                                      size:dataSize 
                                                timestamp:timestamp];
                    free(previewData);
                }
#endif
            }
        }
        @finally {
            CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
        }
    }
}

- (void)startPublishingWithName:(NSString *)streamName {
    if (!streamName) streamName = @"live";
    self.currentStreamName = streamName;
    self.isPublishing = YES;
}

- (void)stopPublishing {
    self.isPublishing = NO;
    self.currentStreamName = nil;
}

@end

// Hooks for Camera Integration
%hook AVCaptureSession

- (void)startRunning {
    %orig;
    [[RTMPCameraManager sharedManager] startServer];
}

- (void)stopRunning {
    [[RTMPCameraManager sharedManager] stopServer];
    %orig;
}

%end

%hook AVCaptureVideoDataOutput

- (void)setSampleBufferDelegate:(id<AVCaptureVideoDataOutputSampleBufferDelegate>)sampleBufferDelegate queue:(dispatch_queue_t)queue {
    %orig(sampleBufferDelegate, queue);
    
    // Hook the sample buffer delegate to intercept frames
    if (sampleBufferDelegate && [sampleBufferDelegate respondsToSelector:@selector(captureOutput:didOutputSampleBuffer:fromConnection:)]) {
        MSHookMessageEx(object_getClass(sampleBufferDelegate),
                       @selector(captureOutput:didOutputSampleBuffer:fromConnection:),
                       (IMP)&replaced_captureOutput,
                       (IMP*)&original_captureOutput);
    }
}

%end

// Replacement method for capturing video frames
static void replaced_captureOutput(id self, SEL _cmd, AVCaptureOutput *captureOutput, CMSampleBufferRef sampleBuffer, AVCaptureConnection *connection) {
    @autoreleasepool {
        // Retain buffer antes de passar para o processamento assíncrono
        CFRetain(sampleBuffer);
        
        // Call original method
        original_captureOutput(self, _cmd, captureOutput, sampleBuffer, connection);
        
        // Process frame em uma closure que gerencia a vida útil do buffer
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0), ^{
            @autoreleasepool {
                [[RTMPCameraManager sharedManager] injectVideoFrame:sampleBuffer];
                CFRelease(sampleBuffer);
            }
        });
    }
}

%ctor {
    @autoreleasepool {
        // Initialize when tweak loads
        NSLog(@"[RTMPCamera] Tweak initialized");
        [RTMPCameraManager sharedManager];
    }
}