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
    
    if (!rtmpServer) {
        // Inicializa servidor RTMP
        rtmpServer = rtmp_server_create(1935);  // Porta padrão RTMP
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

        rtmp_log(RTMP_LOG_INFO, "RTMP server started on port 1935");

        // Configura o preview na thread principal
        dispatch_async(dispatch_get_main_queue(), ^{
            UIWindow *window = [UIApplication sharedApplication].keyWindow;
            CGRect previewFrame = CGRectMake(20, 60, 160, 90);  // Tamanho inicial
            
            previewView = [[RTMPPreviewView alloc] initWithFrame:previewFrame];
            [previewView setupPreviewWithSize:CGSizeMake(1280, 720)];  // Tamanho padrão inicial
            [window addSubview:previewView];
            [previewView startPreview];
            
            rtmp_log(RTMP_LOG_INFO, "Preview view initialized");
        });
    }
}

- (void)stopRunning {
    if (rtmpServer) {
        rtmp_server_stop(rtmpServer);
        rtmp_server_destroy(rtmpServer);
        rtmpServer = nil;
        rtmp_log(RTMP_LOG_INFO, "RTMP server stopped");
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

// Hook para capturar dados da câmera
%hook AVCaptureVideoDataOutput

- (void)setSampleBufferDelegate:(id<AVCaptureVideoDataOutputSampleBufferDelegate>)sampleBufferDelegate queue:(dispatch_queue_t)queue {
    %orig;
    
    rtmp_log(RTMP_LOG_DEBUG, "Video data output delegate set");
}

%end

// Construtor do tweak
%ctor {
    @autoreleasepool {
        rtmp_log(RTMP_LOG_INFO, "RTMPCamera tweak initialized");
        %init;
    }
}