#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>

#include "rtmp_core.h"
#include "rtmp_log.h"
#include "rtmp_preview.h"
#include "rtmp_stream.h"

static RTMPStream* rtmpStream = NULL;
static int serverStarted = 0;

%group RTMPCameraTweak

%hook AVCaptureSession

- (void)startRunning {
    %orig;
    
    if (!serverStarted) {
        // Inicializar logger
        init_logger();
        LOG_INFO("RTMPCamera tweak initialized");
        
        // Inicializar server RTMP
        rtmp_server_start(1935);
        serverStarted = 1;
        
        // Criar stream RTMP
        rtmpStream = rtmp_stream_create();
        
        // Inicializar preview
        rtmp_preview_init();
        
        LOG_INFO("RTMP Server started on port 1935");
    }
}

- (void)stopRunning {
    if (serverStarted) {
        rtmp_server_stop();
        serverStarted = 0;
        
        if (rtmpStream) {
            rtmp_stream_destroy(rtmpStream);
            rtmpStream = NULL;
        }
        
        close_logger();
    }
    
    %orig;
}

%end

// Hook para capturar o output da camera
%hook AVCaptureVideoDataOutput

- (void)setSampleBufferDelegate:(id)delegate queue:(dispatch_queue_t)queue {
    %orig;
    LOG_DEBUG("Video delegate set");
}

%end

// Hook para capturar o output de audio
%hook AVCaptureAudioDataOutput

- (void)setSampleBufferDelegate:(id)delegate queue:(dispatch_queue_t)queue {
    %orig;
    LOG_DEBUG("Audio delegate set");
}

%end

%end

%ctor {
    @autoreleasepool {
        LOG_INFO("RTMPCamera tweak loading...");
        %init(RTMPCameraTweak);
    }
}