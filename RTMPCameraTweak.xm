#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>

#include "rtmp_core.h"
#include "rtmp_log.h"
#include "rtmp_preview.h"
#include "rtmp_stream.h"

static RTMPStream* rtmpStream = NULL;
static int serverStarted = 0;

// Função auxiliar para obter o bundle identifier
static NSString* getCurrentBundleID() {
    NSBundle* mainBundle = [NSBundle mainBundle];
    return mainBundle ? [mainBundle bundleIdentifier] : @"Unknown";
}

%hook AVCaptureSession

- (id)init {
    id result = %orig;
    NSString* bundleID = getCurrentBundleID();
    LOG_INFO("AVCaptureSession init in process: %@", bundleID);
    return result;
}

- (void)startRunning {
    NSString* bundleID = getCurrentBundleID();
    LOG_INFO("AVCaptureSession startRunning in process: %@", bundleID);
    
    if (!serverStarted) {
        LOG_INFO("Initializing RTMP Server...");
        
        // Inicializar logger
        init_logger();
        
        // Inicializar server RTMP
        if (rtmp_server_start(1935) != 0) {
            LOG_ERROR("Failed to start RTMP server on port 1935");
            %orig;
            return;
        }
        serverStarted = 1;
        LOG_INFO("RTMP server started successfully on port 1935");
        
        // Criar stream RTMP
        rtmpStream = rtmp_stream_create();
        if (!rtmpStream) {
            LOG_ERROR("Failed to create RTMP stream");
            rtmp_server_stop();
            serverStarted = 0;
            %orig;
            return;
        }
        LOG_INFO("RTMP stream created successfully");
        
        // Inicializar preview
        dispatch_async(dispatch_get_main_queue(), ^{
            rtmp_preview_init();
            LOG_INFO("Preview window initialized");
        });
    }
    
    %orig;
}

- (void)stopRunning {
    NSString* bundleID = getCurrentBundleID();
    LOG_INFO("AVCaptureSession stopRunning in process: %@", bundleID);
    
    if (serverStarted) {
        LOG_INFO("Stopping RTMP server...");
        rtmp_server_stop();
        serverStarted = 0;
        
        if (rtmpStream) {
            rtmp_stream_destroy(rtmpStream);
            rtmpStream = NULL;
        }
        
        dispatch_async(dispatch_get_main_queue(), ^{
            rtmp_preview_hide();
        });
        
        close_logger();
        LOG_INFO("RTMP server stopped successfully");
    }
    
    %orig;
}

%end

%hook AVCaptureVideoDataOutput

- (void)setSampleBufferDelegate:(id)delegate queue:(dispatch_queue_t)queue {
    NSString* bundleID = getCurrentBundleID();
    LOG_DEBUG("Video delegate set in process: %@", bundleID);
    %orig;
}

%end

%hook AVCaptureAudioDataOutput

- (void)setSampleBufferDelegate:(id)delegate queue:(dispatch_queue_t)queue {
    NSString* bundleID = getCurrentBundleID();
    LOG_DEBUG("Audio delegate set in process: %@", bundleID);
    %orig;
}

%end

%ctor {
    @autoreleasepool {
        NSString* bundleID = getCurrentBundleID();
        
        // Inicializar logger primeiro para capturar todos os logs
        init_logger();
        LOG_INFO("RTMPCamera tweak loading in process: %@", bundleID);
        
        // Inicializar hooks
        %init;
        
        LOG_INFO("RTMPCamera tweak initialized successfully in: %@", bundleID);
    }
}