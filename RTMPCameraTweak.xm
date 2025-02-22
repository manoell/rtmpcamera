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
static BOOL isStreamActive = NO;
static BOOL serverStarted = NO;
static BOOL previewStarted = NO;

// Default configuration
static NSString *defaultRtmpPort = @"1935";
static NSString *defaultStreamPath = @"/live/stream";

// Forward declarations
static void setupRTMPServer(void);
static void setupRTMPStream(void);
static void handleRTMPEvent(const RTMPCoreEvent *event, void *context);
static void handleServerEvent(const RTMPServerEvent *event, void *context);

// Server event handler
static void handleServerEvent(const RTMPServerEvent *event, void *context) {
    switch (event->type) {
        case RTMP_SERVER_EVENT_STARTED:
            NSLog(@"[RTMPCamera] RTMP Server started successfully");
            serverStarted = YES;
            
            // Start preview window after server starts
            if (!previewStarted) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    rtmp_preview_show(rtmpStream);
                    previewStarted = YES;
                });
            }
            break;
            
        case RTMP_SERVER_EVENT_CLIENT_CONNECTED:
            NSLog(@"[RTMPCamera] Client connected to RTMP Server");
            break;
            
        case RTMP_SERVER_EVENT_STREAM_STARTED:
            NSLog(@"[RTMPCamera] Stream started");
            isStreamActive = YES;
            break;
            
        case RTMP_SERVER_EVENT_STREAM_ENDED:
            NSLog(@"[RTMPCamera] Stream ended");
            isStreamActive = NO;
            break;
            
        case RTMP_SERVER_EVENT_ERROR:
            NSLog(@"[RTMPCamera] Server error: %s", event->error_message);
            break;
    }
}

// RTMP core event handler
static void handleRTMPEvent(const RTMPCoreEvent *event, void *context) {
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
}

// Initialize RTMP server
static void setupRTMPServer(void) {
    if (serverStarted) return;
    
    // Create and configure server
    serverContext = rtmp_server_create();
    if (!serverContext) {
        NSLog(@"[RTMPCamera] Failed to create RTMP server");
        return;
    }
    
    // Set up server callback
    rtmp_server_set_callback(serverContext, handleServerEvent, NULL);
    
    // Configure server port and path
    if (!rtmp_server_configure(serverContext, [defaultRtmpPort UTF8String], [defaultStreamPath UTF8String])) {
        NSLog(@"[RTMPCamera] Failed to configure RTMP server");
        rtmp_server_destroy(serverContext);
        serverContext = nil;
        return;
    }
    
    // Start server
    if (!rtmp_server_start(serverContext)) {
        NSLog(@"[RTMPCamera] Failed to start RTMP server");
        rtmp_server_destroy(serverContext);
        serverContext = nil;
        return;
    }
}

// Initialize RTMP stream handling
static void setupRTMPStream(void) {
    if (rtmpContext) return;
    
    // Initialize RTMP core
    rtmpContext = rtmp_core_create();
    if (!rtmpContext) {
        NSLog(@"[RTMPCamera] Failed to create RTMP context");
        return;
    }
    
    // Set up diagnostics
    rtmp_diagnostic_init();
    rtmp_diagnostic_set_level(1); // INFO level
    
    // Create RTMP stream
    NSString *rtmpUrl = [NSString stringWithFormat:@"rtmp://localhost:%@%@", defaultRtmpPort, defaultStreamPath];
    rtmpStream = rtmp_stream_create([rtmpUrl UTF8String]);
    if (!rtmpStream) {
        NSLog(@"[RTMPCamera] Failed to create RTMP stream");
        rtmp_core_destroy(rtmpContext);
        rtmpContext = nil;
        return;
    }
    
    // Configure stream
    rtmp_stream_set_video_params(rtmpStream, 1920, 1080, 30, 2000000); // 1080p30 @ 2Mbps
    
    // Add stream to core
    rtmp_core_add_stream(rtmpContext, rtmpStream);
    
    // Set callback
    rtmp_core_set_callback(rtmpContext, handleRTMPEvent, NULL);
    
    // Create camera compatibility layer
    cameraCompat = [[RTMPCameraCompat alloc] init];
}

%hook AVCaptureDevice

- (AVCaptureDeviceInput *)deviceInputWithError:(NSError **)outError {
    // Return original input if stream is not active
    if (!isStreamActive) {
        return %orig;
    }
    
    // Configure virtual camera when stream is active
    if (cameraCompat) {
        [cameraCompat configureVirtualCameraWithStream:rtmpStream];
        return [cameraCompat createVirtualDeviceInput];
    }
    
    return %orig;
}

%end

%hook AVCaptureSession

- (void)startRunning {
    // Start normal camera session if stream is not active
    if (!isStreamActive) {
        %orig;
        return;
    }
    
    // Update camera compatibility when starting session
    if (cameraCompat) {
        [cameraCompat updateStreamMetadata:rtmpStream];
    }
    
    %orig;
}

%end

%hook AVCaptureVideoDataOutput

- (void)setSampleBufferDelegate:(id<AVCaptureVideoDataOutputSampleBufferDelegate>)sampleBufferDelegate queue:(dispatch_queue_t)sampleBufferCallbackQueue {
    // Create interceptor for camera frames
    id originalDelegate = sampleBufferDelegate;
    id interceptor = ^(CMSampleBufferRef sampleBuffer) {
        // Use original camera if stream is not active
        if (!isStreamActive) {
            if ([originalDelegate respondsToSelector:@selector(captureOutput:didOutputSampleBuffer:fromConnection:)]) {
                [originalDelegate captureOutput:nil didOutputSampleBuffer:sampleBuffer fromConnection:nil];
            }
            return;
        }
        
        // Process frame through RTMP stream
        if (rtmpStream && rtmpStream->connected) {
            CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
            if (imageBuffer) {
                // Update preview
                rtmp_preview_update_frame(imageBuffer);
                
                // Push frame to stream
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
        
        // Initialize components
        setupRTMPStream();
        setupRTMPServer();
        
        NSLog(@"[RTMPCamera] Tweak initialized successfully");
    }
}