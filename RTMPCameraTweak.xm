// RTMPCameraTweak.xm
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#import "rtmp_core.h"
#import "rtmp_preview.h"

static RTMPServer* rtmpServer = nil;
static RTMPPreviewView* previewView = nil;

static void initializeRTMPServer() {
    if (!rtmpServer) {
        rtmp_log(RTMP_LOG_INFO, "Initializing RTMP server...");
        
        // Cria o servidor na porta 1935
        rtmpServer = rtmp_server_create(1935);
        if (!rtmpServer) {
            rtmp_log(RTMP_LOG_ERROR, "Failed to create RTMP server");
            return;
        }
        
        // Inicia o servidor
        int status = rtmp_server_start(rtmpServer);
        if (status != RTMP_OK) {
            rtmp_log(RTMP_LOG_ERROR, "Failed to start RTMP server: %d", status);
            rtmp_server_destroy(rtmpServer);
            rtmpServer = nil;
            return;
        }
        
        rtmp_log(RTMP_LOG_INFO, "RTMP server started successfully on port 1935");
        
        // Inicializa o preview
        dispatch_async(dispatch_get_main_queue(), ^{
            UIWindow *window = [UIApplication sharedApplication].keyWindow;
            if (!window) {
                rtmp_log(RTMP_LOG_ERROR, "No key window found");
                return;
            }
            
            CGRect frame = CGRectMake(20, 60, 160, 90);
            previewView = [[RTMPPreviewView alloc] initWithFrame:frame];
            [previewView setupPreviewWithSize:CGSizeMake(1280, 720)];
            previewView.layer.cornerRadius = 8;
            previewView.clipsToBounds = YES;
            
            UIPanGestureRecognizer *pan = [[UIPanGestureRecognizer alloc] 
                initWithTarget:previewView 
                action:@selector(handlePan:)];
            [previewView addGestureRecognizer:pan];
            
            [window addSubview:previewView];
            [window bringSubviewToFront:previewView];
            [previewView startPreview];
            
            rtmp_log(RTMP_LOG_INFO, "Preview view initialized");
        });
    }
}

%ctor {
    @autoreleasepool {
        rtmp_log(RTMP_LOG_INFO, "RTMPCamera tweak starting...");
        
        // Inicializa o servidor RTMP e preview após um pequeno delay
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), 
                      dispatch_get_main_queue(), ^{
            initializeRTMPServer();
        });
        
        %init;
        rtmp_log(RTMP_LOG_INFO, "RTMPCamera tweak initialized");
    }
}