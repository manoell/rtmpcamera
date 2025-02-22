#ifndef RTMP_CAMERA_COMPAT_H
#define RTMP_CAMERA_COMPAT_H

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

#define MAX_SUPPORTED_FORMATS 32

// Forward declaration of RTMPStream
typedef struct RTMPStream RTMPStream;

typedef enum {
    CAMERA_CONTROL_FOCUS,
    CAMERA_CONTROL_EXPOSURE,
    CAMERA_CONTROL_ISO,
    CAMERA_CONTROL_ZOOM,
    CAMERA_CONTROL_WHITE_BALANCE
} CameraControlCommand;

@interface RTMPCameraCompat : NSObject

// Initialize and configure virtual camera properties
- (instancetype)init;
- (void)configureVirtualCameraWithStream:(RTMPStream *)stream;

// Update stream metadata and properties
- (void)updateStreamMetadata:(RTMPStream *)stream;
- (void)handleCameraControl:(RTMPStream *)stream command:(CameraControlCommand)command value:(float)value;

@end

#endif // RTMP_CAMERA_COMPAT_H