// rtmp_camera_compat.h
#ifndef RTMP_CAMERA_COMPAT_H
#define RTMP_CAMERA_COMPAT_H

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <UIKit/UIKit.h>

@class RTMPCameraSettings;

// Forward declarations for private APIs
@interface AVCaptureDeviceFormat : NSObject
@property (nonatomic, readonly) CMVideoFormatDescriptionRef formatDescription;
@property (nonatomic, readonly) NSArray *videoSupportedFrameRateRanges;
@end

@interface AVFrameRateRange : NSObject
@property (nonatomic, readonly) float minFrameRate;
@property (nonatomic, readonly) float maxFrameRate;
@end

@interface AVCaptureDevice (PrivateAPI)
- (AVCaptureDeviceFormat *)activeFormat;
@end

// Camera states
typedef NS_ENUM(NSInteger, RTMPCameraState) {
    RTMPCameraStateOff = 0,
    RTMPCameraStateStarting,
    RTMPCameraStateRunning,
    RTMPCameraStateError,
    RTMPCameraStateStopping
};

// Camera settings 
@interface RTMPCameraSettings : NSObject

@property (nonatomic, assign) CGSize resolution;
@property (nonatomic, assign) float frameRate;
@property (nonatomic, assign) BOOL autoFocus;
@property (nonatomic, assign) BOOL autoExposure;
@property (nonatomic, assign) BOOL autoWhiteBalance;
@property (nonatomic, assign) float zoom;
@property (nonatomic, assign) float exposure;
@property (nonatomic, assign) float iso;
@property (nonatomic, assign) float focusPoint;
@property (nonatomic, assign) CGPoint exposurePoint;
@property (nonatomic, assign) AVCaptureDevicePosition position;

+ (instancetype)defaultSettings;
- (BOOL)isEqual:(RTMPCameraSettings *)other;
- (NSDictionary *)serialize;
+ (instancetype)settingsWithDictionary:(NSDictionary *)dict;

@end

// Camera stats
@interface RTMPCameraStats : NSObject

@property (nonatomic, assign) CGSize resolution;
@property (nonatomic, assign) float frameRate;
@property (nonatomic, assign) float currentFPS;
@property (nonatomic, assign) uint64_t frameCount;
@property (nonatomic, assign) uint64_t droppedFrames;
@property (nonatomic, assign) uint64_t totalBytes;
@property (nonatomic, assign) float bitrate;
@property (nonatomic, assign) NSTimeInterval uptime;
@property (nonatomic, assign) BOOL hasVideo;
@property (nonatomic, assign) BOOL hasAudio;
@property (nonatomic, assign) BOOL isPublishing;

- (NSDictionary *)serialize;
+ (instancetype)statsWithDictionary:(NSDictionary *)dict;

@end

// Camera compatability delegate
@protocol RTMPCameraCompatibilityDelegate <NSObject>
@optional
- (void)cameraStateDidChange:(RTMPCameraState)state;
- (void)cameraDidUpdateStats:(RTMPCameraStats *)stats;
- (void)cameraDidEncounterError:(NSError *)error;
@end

// Main camera compatibility interface
@interface RTMPCameraCompatibility : NSObject

// Singleton
+ (instancetype)sharedInstance;

// State
@property (nonatomic, readonly) RTMPCameraState state;
@property (nonatomic, strong, readonly) RTMPCameraStats *stats;
@property (nonatomic, strong, readonly) RTMPCameraSettings *settings;
@property (nonatomic, weak) id<RTMPCameraCompatibilityDelegate> delegate;

// Control methods
- (void)startWithSettings:(RTMPCameraSettings *)settings;
- (void)stop;
- (void)restart;
- (BOOL)isRunning;
- (void)updateSettings:(RTMPCameraSettings *)settings;

// Frame processing
- (void)processRTMPFrame:(void*)frameData 
                   size:(size_t)frameSize 
              timestamp:(uint32_t)timestamp 
            isKeyframe:(BOOL)isKeyframe;

- (void)flushBuffers;
- (CVPixelBufferRef)copyLastFrame;

// Preview
- (UIView *)previewView;
- (void)startPreview;
- (void)stopPreview;
- (void)updatePreviewOrientation;

// Utilities
- (NSArray<NSValue *> *)supportedResolutions;
- (NSArray<NSNumber *> *)supportedFrameRates;
- (BOOL)supportsCamera:(AVCaptureDevicePosition)position;
- (BOOL)hasMultipleCameras;
- (AVCaptureDevice *)currentDevice;

@end

// C interface for bridging
#ifdef __cplusplus
extern "C" {
#endif

bool rtmp_camera_compat_initialize(void);
void rtmp_camera_compat_cleanup(void);

void rtmp_camera_compat_start(void);
void rtmp_camera_compat_stop(void);
bool rtmp_camera_compat_is_running(void);

void rtmp_camera_compat_process_frame(void* frame_data, 
                                    size_t frame_size,
                                    uint32_t timestamp,
                                    bool is_keyframe);

bool rtmp_camera_compat_get_resolution(int* width, int* height);
float rtmp_camera_compat_get_framerate(void);
uint64_t rtmp_camera_compat_get_frame_count(void);
bool rtmp_camera_compat_is_publishing(void);

#ifdef __cplusplus
}
#endif

#endif /* RTMP_CAMERA_COMPAT_H */