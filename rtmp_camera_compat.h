#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

@interface RTMPCameraController : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>

// Singleton instance
+ (instancetype)sharedInstance;

// RTMP session control
- (void)startRTMPSession;
- (void)stopRTMPSession;

// Camera settings control
- (void)backupOriginalCameraSettings;
- (void)restoreOriginalCameraSettings;

// Stream monitoring
- (void)startStreamMonitoring;
- (void)updateStreamMetrics;

@end

// Notification names
extern NSString *const RTMPCameraStreamStartedNotification;
extern NSString *const RTMPCameraStreamStoppedNotification;
extern NSString *const RTMPCameraStreamErrorNotification;

// Error domain
extern NSString *const RTMPCameraErrorDomain;

// Error codes
typedef NS_ENUM(NSInteger, RTMPCameraError) {
    RTMPCameraErrorUnknown = -1,
    RTMPCameraErrorNoCamera = -2,
    RTMPCameraErrorPermissionDenied = -3,
    RTMPCameraErrorStreamFailed = -4,
    RTMPCameraErrorInvalidState = -5
};