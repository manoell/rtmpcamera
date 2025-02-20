// RTMPCameraTweak.xm
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#import "rtmp_core.h"
#import "rtmp_preview.h"
#import "rtmp_session.h"
#import "rtmp_stream.h"

// Variáveis globais
static RTMPServer* rtmpServer = nil;
static RTMPPreviewView* previewView = nil;

%hook AVCaptureSession

- (void)startRunning {
    %orig;
    
    rtmp_log(RTMP_LOG_INFO, "AVCaptureSession startRunning called");
    
    if (!rtmpServer) {
        // Inicializa servidor RTMP
        rtmpServer = rtmp_server_create(1935);
        if (!rtmpServer) {
            rtmp_log(RTMP_LOG_ERROR, "Failed to create RTMP server");
            return;
        }
        rtmp_log(RTMP_LOG_INFO, "RTMP server created");

        // Inicia o servidor
        int status = rtmp_server_start(rtmpServer);
        if (status != RTMP_OK) {
            rtmp_log(RTMP_LOG_ERROR, "Failed to start RTMP server: %d", status);
            rtmp_server_destroy(rtmpServer);
            rtmpServer = nil;
            return;
        }
        rtmp_log(RTMP_LOG_INFO, "RTMP server started successfully on port 1935");

        // Configura preview na main thread
        dispatch_async(dispatch_get_main_queue(), ^{
            rtmp_log(RTMP_LOG_INFO, "Setting up preview view");
            
            UIWindow *window = [UIApplication sharedApplication].keyWindow;
            if (!window) {
                rtmp_log(RTMP_LOG_ERROR, "Failed to get key window");
                return;
            }
            
            CGRect previewFrame = CGRectMake(20, 60, 160, 90);
            previewView = [[RTMPPreviewView alloc] initWithFrame:previewFrame];
            if (!previewView) {
                rtmp_log(RTMP_LOG_ERROR, "Failed to create preview view");
                return;
            }
            
            [previewView setupPreviewWithSize:CGSizeMake(1280, 720)];
            [window addSubview:previewView];
            [previewView startPreview];
            
            rtmp_log(RTMP_LOG_INFO, "Preview view initialized and added to window");
            
            // Adiciona gesture recognizer
            UIPanGestureRecognizer *panGesture = [[UIPanGestureRecognizer alloc] 
                                                 initWithTarget:previewView 
                                                 action:@selector(handlePan:)];
            [previewView addGestureRecognizer:panGesture];
            
            // Traz preview para frente
            [window bringSubviewToFront:previewView];
        });
    }
}

- (void)stopRunning {
    rtmp_log(RTMP_LOG_INFO, "AVCaptureSession stopRunning called");
    
    if (rtmpServer) {
        rtmp_server_stop(rtmpServer);
        rtmp_server_destroy(rtmpServer);
        rtmpServer = nil;
        rtmp_log(RTMP_LOG_INFO, "RTMP server stopped and destroyed");
    }
    
    if (previewView) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [previewView stopPreview];
            [previewView removeFromSuperview];
            previewView = nil;
            rtmp_log(RTMP_LOG_INFO, "Preview view stopped and removed");
        });
    }
    
    %orig;
}

%end

%hook AVCaptureVideoDataOutput

- (void)setSampleBufferDelegate:(id<AVCaptureVideoDataOutputSampleBufferDelegate>)sampleBufferDelegate queue:(dispatch_queue_t)queue {
    rtmp_log(RTMP_LOG_INFO, "Setting sample buffer delegate");
    %orig;
}

%end

%ctor {
    @autoreleasepool {
        rtmp_log(RTMP_LOG_INFO, "RTMPCamera tweak initialized");
        %init;
    }
}