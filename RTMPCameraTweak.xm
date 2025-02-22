#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#import "rtmp_core.h"
#import "rtmp_stream.h"
#import "rtmp_camera_compat.h"
#import "rtmp_preview.h"
#import "rtmp_server_integration.h"

// Global state
static RTMPContext *rtmpContext = nil;
static RTMPStream *rtmpStream = nil;
static RTMPCameraCompat *cameraCompat = nil;
static RTMPServerContext *serverContext = nil;
static BOOL isServerReady = NO;
static BOOL isStreamActive = NO;
static BOOL previewEnabled = NO;
static dispatch_queue_t rtmpQueue;

// Configuration
static NSString *const kDefaultRtmpPort = @"1935";
static NSString *const kDefaultStreamPath = @"/live/stream";
static const NSTimeInterval kServerStartTimeout = 10.0;

// Forward declarations
static void setupRTMPServer(void);
static void setupRTMPStream(void);
static void startPreview(void);
static void stopPreview(void);
static void handleRTMPEvent(const RTMPCoreEvent *event, void *context);
static void handleServerEvent(const RTMPServerEvent *event, void *context);

// Server event handler
static void handleServerEvent(const RTMPServerEvent *event, void *context) {
    dispatch_async(rtmpQueue, ^{
        switch (event->type) {
            case RTMP_SERVER_EVENT_STARTED:
                NSLog(@"[RTMPCamera] RTMP Server started successfully");
                isServerReady = YES;
                
                // Start preview after server is ready
                if (!previewEnabled) {
                    startPreview();
                }
                break;
                
            case RTMP_SERVER_EVENT_CLIENT_CONNECTED:
                NSLog(@"[RTMPCamera] Client connected to RTMP Server");
                break;
                
            case RTMP_SERVER_EVENT_STREAM_STARTED:
                NSLog(@"[RTMPCamera] Stream started - Feed replacement active");
                isStreamActive = YES;
                break;
                
            case RTMP_SERVER_EVENT_STREAM_ENDED:
                NSLog(@"[RTMPCamera] Stream ended - Reverting to normal camera");
                isStreamActive = NO;
                break;
                
            case RTMP_SERVER_EVENT_ERROR:
                NSLog(@"[RTMPCamera] Server error: %s", event->error_message);
                // Attempt recovery
                if (!isServerReady) {
                    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC), rtmpQueue, ^{
                        setupRTMPServer();
                    });
                }
                break;
        }
    });
}

// RTMP core event handler
static void handleRTMPEvent(const RTMPCoreEvent *event, void *context) {
    dispatch_async(rtmpQueue, ^{
        switch (event->type) {
            case RTMP_CORE_EVENT_CONNECTED:
                NSLog(@"[RTMPCamera] RTMP core connected");
                rtmp_stream_set_connected(rtmpStream, true);
                break;
                
            case RTMP_CORE_EVENT_DISCONNECTED:
                NSLog(@"[RTMPCamera] RTMP core disconnected");
                rtmp_stream_set_connected(rtmpStream, false);
                isStreamActive = NO;
                break;
                
            case RTMP_CORE_EVENT_ERROR:
                NSLog(@"[RTMPCamera] RTMP core error: %s", event->error_message);
                break;
        }
    });
}

// Initialize RTMP server
static void setupRTMPServer(void) {
    if (serverContext) {
        rtmp_server_destroy(serverContext);
        serverContext = nil;
    }
    
    serverContext = rtmp_server_create();
    if (!serverContext) {
        NSLog(@"[RTMPCamera] Failed to create RTMP server");
        return;
    }
    
    rtmp_server_set_callback(serverContext, handleServerEvent, NULL);
    
    if (!rtmp_server_configure(serverContext, 
                             [kDefaultRtmpPort UTF8String], 
                             [kDefaultStreamPath UTF8String])) {
        NSLog(@"[RTMPCamera] Failed to configure RTMP server");
        rtmp_server_destroy(serverContext);
        serverContext = nil;
        return;
    }
    
    if (!rtmp_server_start(serverContext)) {
        NSLog(@"[RTMPCamera] Failed to start RTMP server");
        rtmp_server_destroy(serverContext);
        serverContext = nil;
        return;
    }
    
    NSLog(@"[RTMPCamera] RTMP Server initialized");
}

// Initialize RTMP stream
static void setupRTMPStream(void) {
    if (rtmpContext) {
        rtmp_core_destroy(rtmpContext);
        rtmpContext = nil;
    }
    
    rtmpContext = rtmp_core_create();
    if (!rtmpContext) {
        NSLog(@"[RTMPCamera] Failed to create RTMP context");
        return;
    }
    
    rtmp_diagnostic_init();
    rtmp_diagnostic_set_level(1);
    
    NSString *rtmpUrl = [NSString stringWithFormat:@"rtmp://localhost:%@%@", 
                        kDefaultRtmpPort, kDefaultStreamPath];
    rtmpStream = rtmp_stream_create([rtmpUrl UTF8String]);
    if (!rtmpStream) {
        NSLog(@"[RTMPCamera] Failed to create RTMP stream");
        rtmp_core_destroy(rtmpContext);
        rtmpContext = nil;
        return;
    }
    
    rtmp_stream_set_video_params(rtmpStream, 1920, 1080, 30, 2000000);
    rtmp_core_add_stream(rtmpContext, rtmpStream);
    rtmp_core_set_callback(rtmpContext, handleRTMPEvent, NULL);
    
    if (!cameraCompat) {
        cameraCompat = [[RTMPCameraCompat alloc] init];
    }
    
    NSLog(@"[RTMPCamera] RTMP Stream initialized");
}

static void startPreview(void) {
    if (!previewEnabled && rtmpStream) {
        dispatch_async(dispatch_get_main_queue(), ^{
            rtmp_preview_show(rtmpStream);
            previewEnabled = YES;
        });
    }
}

static void stopPreview(void) {
    if (previewEnabled) {
        dispatch_async(dispatch_get_main_queue(), ^{
            rtmp_preview_hide();
            previewEnabled = NO;
        });
    }
}

%hook AVCaptureDevice

- (AVCaptureDeviceInput *)deviceInputWithError:(NSError **)outError {
    if (!isStreamActive || !cameraCompat) {
        return %orig;
    }
    
    [cameraCompat configureVirtualCameraWithStream:rtmpStream];
    return [cameraCompat createVirtualDeviceInput];
}

%end

%hook AVCaptureSession

- (void)startRunning {
    %orig;
    
    if (isStreamActive && cameraCompat) {
        [cameraCompat updateStreamMetadata:rtmpStream];
    }
}

%end

%hook AVCaptureVideoDataOutput

- (void)setSampleBufferDelegate:(id<AVCaptureVideoDataOutputSampleBufferDelegate>)sampleBufferDelegate queue:(dispatch_queue_t)sampleBufferCallbackQueue {
    id originalDelegate = sampleBufferDelegate;
    id interceptor = ^(CMSampleBufferRef sampleBuffer) {
        if (!isStreamActive) {
            if ([originalDelegate respondsToSelector:@selector(captureOutput:didOutputSampleBuffer:fromConnection:)]) {
                [originalDelegate captureOutput:nil didOutputSampleBuffer:sampleBuffer fromConnection:nil];
            }
            return;
        }
        
        if (rtmpStream && rtmpStream->connected) {
            CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
            if (imageBuffer) {
                if (previewEnabled) {
                    rtmp_preview_update_frame(imageBuffer);
                }
                
                CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
                void *baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);
                size_t size = CVPixelBufferGetDataSize(imageBuffer);
                
                rtmp_stream_push_frame(rtmpStream, baseAddress, size, 
                                     CMSampleBufferGetPresentationTimeStamp(sampleBuffer).value,
                                     true);
                
                CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
            }
        }
    };
    
    %orig(interceptor, sampleBufferCallbackQueue);
}

%end

%ctor {
    @autoreleasepool {
        NSLog(@"[RTMPCamera] Initializing tweak");
        
        rtmpQueue = dispatch_queue_create("com.rtmpcamera.queue", DISPATCH_QUEUE_SERIAL);
        
        dispatch_async(rtmpQueue, ^{
            setupRTMPServer();
            
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1 * NSEC_PER_SEC)), rtmpQueue, ^{
                setupRTMPStream();
            });
        });
        
        NSLog(@"[RTMPCamera] Tweak initialization completed");
    }
}

%dtor {
    stopPreview();
    
    if (serverContext) {
        rtmp_server_destroy(serverContext);
        serverContext = nil;
    }
    
    if (rtmpContext) {
        rtmp_core_destroy(rtmpContext);
        rtmpContext = nil;
    }
    
    if (rtmpStream) {
        rtmp_stream_destroy(rtmpStream);
        rtmpStream = nil;
    }
    
    cameraCompat = nil;
}