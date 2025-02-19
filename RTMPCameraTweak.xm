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
        if (rtmp_server_start(1935) != 0) {
            LOG_ERROR("Failed to start RTMP server");
            return;
        }
        serverStarted = 1;
        
        // Criar stream RTMP
        rtmpStream = rtmp_stream_create();
        if (!rtmpStream) {
            LOG_ERROR("Failed to create RTMP stream");
            return;
        }
        
        // Inicializar preview
        rtmp_preview_init();
        
        LOG_INFO("RTMP Server started on port 1935");
    }
}

- (void)stopRunning {
    if (serverStarted) {
        LOG_INFO("Stopping RTMP server...");
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
    LOG_DEBUG("Video delegate set for process: %@", [[NSBundle mainBundle] bundleIdentifier]);
}

%end

// Hook para capturar o output de audio
%hook AVCaptureAudioDataOutput

- (void)setSampleBufferDelegate:(id)delegate queue:(dispatch_queue_t)queue {
    %orig;
    LOG_DEBUG("Audio delegate set for process: %@", [[NSBundle mainBundle] bundleIdentifier]);
}

%end

%end

%ctor {
    @autoreleasepool {
        // Inicializar logger para ver se o tweak está sendo carregado
        init_logger();
        
        NSString *bundleID = [[NSBundle mainBundle] bundleIdentifier];
        LOG_INFO("RTMPCamera tweak loading in process: %@", bundleID);
        
        // Inicializar tweak
        %init(RTMPCameraTweak);
        
        LOG_INFO("RTMPCamera tweak initialized successfully");
    }
}