#ifndef RTMP_CAMERA_COMPAT_H
#define RTMP_CAMERA_COMPAT_H

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#import "rtmp_protocol.h"

// Camera capture states
typedef enum {
    RTMP_CAMERA_STATE_IDLE = 0,
    RTMP_CAMERA_STATE_STARTING,
    RTMP_CAMERA_STATE_CAPTURING,
    RTMP_CAMERA_STATE_PAUSED,
    RTMP_CAMERA_STATE_ERROR
} RTMPCameraState;

// Camera orientation
typedef enum {
    RTMP_CAMERA_ORIENTATION_PORTRAIT = 0,
    RTMP_CAMERA_ORIENTATION_LANDSCAPE_LEFT,
    RTMP_CAMERA_ORIENTATION_LANDSCAPE_RIGHT,
    RTMP_CAMERA_ORIENTATION_PORTRAIT_UPSIDE_DOWN
} RTMPCameraOrientation;

// Camera configuration
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t frameRate;
    uint32_t bitrate;
    uint32_t keyframeInterval;
    float jpegQuality;
    bool enableHardwareEncoder;
    bool enableFaceDetection;
    bool enableStabilization;
    bool maintainAspectRatio;
    RTMPCameraOrientation orientation;
    AVCaptureDevicePosition position;
} RTMPCameraConfig;

// Camera status
typedef struct {
    RTMPCameraState state;
    uint32_t framesCapture;
    uint32_t framesEncoded;
    uint32_t framesSent;
    uint32_t framesDropped;
    uint32_t currentBitrate;
    uint32_t currentFPS;
    float cpuUsage;
    float memoryUsage;
    uint32_t captureTime;
    uint32_t encodeTime;
    uint32_t sendTime;
} RTMPCameraStatus;

@interface RTMPCameraCompatibility : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate,
                                             AVCaptureAudioDataOutputSampleBufferDelegate>

// Singleton instance
+ (instancetype)sharedInstance;

// Camera control
- (BOOL)startCapture;
- (void)stopCapture;
- (void)pauseCapture;
- (void)resumeCapture;

// Configuration
- (void)setConfig:(RTMPCameraConfig *)config;
- (RTMPCameraConfig *)getConfig;
- (void)setPreviewView:(UIView *)view;
- (void)switchCamera;

// Camera properties
- (void)setExposurePoint:(CGPoint)point;
- (void)setFocusPoint:(CGPoint)point;
- (void)setZoomLevel:(float)level;
- (void)setTorchMode:(AVCaptureTorchMode)mode;
- (void)setWhiteBalanceMode:(AVCaptureWhiteBalanceMode)mode;
- (void)setVideoStabilizationMode:(AVCaptureVideoStabilizationMode)mode;

// Status and monitoring
- (RTMPCameraState)getState;
- (RTMPCameraStatus *)getStatus;
- (void)resetStats;
- (BOOL)isHardwareEncoderAvailable;
- (void)notifyBitrateChange:(uint32_t)bitrate;
- (void)notifyFramerateChange:(uint32_t)framerate;

// RTMP streaming
- (void)attachStream:(RTMPStream *)stream;
- (void)detachStream;
- (BOOL)isStreamAttached;

// Events and callbacks
typedef void (^RTMPCameraStateCallback)(RTMPCameraState state);
typedef void (^RTMPCameraErrorCallback)(NSError *error);
typedef void (^RTMPCameraFaceDetectionCallback)(NSArray<AVMetadataFaceObject *> *faces);

- (void)setStateCallback:(RTMPCameraStateCallback)callback;
- (void)setErrorCallback:(RTMPCameraErrorCallback)callback;
- (void)setFaceDetectionCallback:(RTMPCameraFaceDetectionCallback)callback;

// Device management
- (NSArray<AVCaptureDevice *> *)getAvailableDevices;
- (AVCaptureDevice *)getActiveDevice;
- (BOOL)switchToDevice:(AVCaptureDevice *)device;

// Orientation handling
- (void)setOrientation:(RTMPCameraOrientation)orientation;
- (void)handleDeviceOrientationChange:(UIDeviceOrientation)orientation;

@end

#endif /* RTMP_CAMERA_COMPAT_H */