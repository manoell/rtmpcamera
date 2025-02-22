#import "rtmp_camera_compat.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

@implementation RTMPCameraCompat {
    AVCaptureDevice *originalDevice;
    NSDictionary *originalProperties;
    CMTime originalFrameDuration;
    AVCaptureDeviceFormat *originalFormat;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        [self setupOriginalCameraProperties];
    }
    return self;
}

- (void)setupOriginalCameraProperties {
    // Get the original back camera
    originalDevice = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (!originalDevice) return;
    
    // Store original properties
    originalProperties = @{
        @"exposureMode": @(originalDevice.exposureMode),
        @"whiteBalanceMode": @(originalDevice.whiteBalanceMode),
        @"focusMode": @(originalDevice.focusMode),
        @"iso": @(originalDevice.iso),
        @"exposureDuration": [NSValue valueWithCMTime:originalDevice.exposureDuration],
        @"deviceWhiteBalanceGains": [NSValue valueWithCGPoint:CGPointMake(
            originalDevice.deviceWhiteBalanceGains.redGain,
            originalDevice.deviceWhiteBalanceGains.greenGain)],
        @"lensPosition": @(originalDevice.lensPosition)
    };
    
    originalFormat = originalDevice.activeFormat;
    originalFrameDuration = originalDevice.activeVideoMinFrameDuration;
}

- (void)configureVirtualCameraWithStream:(RTMPStream *)stream {
    if (!stream) return;
    
    // Configure stream to match original camera properties
    [self applyOriginalPropertiesToStream:stream];
    
    // Set up format matching
    [self setupFormatMatchingWithStream:stream];
    
    // Configure frame timing
    [self configureFrameTimingWithStream:stream];
}

- (void)applyOriginalPropertiesToStream:(RTMPStream *)stream {
    // Apply exposure settings
    stream->camera_properties.exposure_mode = [originalProperties[@"exposureMode"] intValue];
    stream->camera_properties.exposure_duration = [[originalProperties[@"exposureDuration"] CMTimeValue] timeValue];
    stream->camera_properties.iso = [originalProperties[@"iso"] floatValue];
    
    // Apply white balance
    stream->camera_properties.white_balance_mode = [originalProperties[@"whiteBalanceMode"] intValue];
    CGPoint whiteBalanceGains = [[originalProperties[@"deviceWhiteBalanceGains"] CGPointValue];
    stream->camera_properties.white_balance_gains[0] = whiteBalanceGains.x;
    stream->camera_properties.white_balance_gains[1] = whiteBalanceGains.y;
    
    // Apply focus settings
    stream->camera_properties.focus_mode = [originalProperties[@"focusMode"] intValue];
    stream->camera_properties.lens_position = [originalProperties[@"lensPosition"] floatValue];
}

- (void)setupFormatMatchingWithStream:(RTMPStream *)stream {
    if (!originalFormat) return;
    
    CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(originalFormat.formatDescription);
    stream->video_width = dimensions.width;
    stream->video_height = dimensions.height;
    
    // Match color space and pixel format
    stream->video_color_space = originalFormat.supportedColorSpaces.firstObject;
    stream->video_pixel_format = originalFormat.mediaType;
    
    // Match field of view and zoom
    if ([originalFormat respondsToSelector:@selector(videoFieldOfView)]) {
        stream->camera_properties.field_of_view = originalFormat.videoFieldOfView;
    }
    
    if ([originalFormat respondsToSelector:@selector(videoZoomFactor)]) {
        stream->camera_properties.zoom_factor = originalFormat.videoZoomFactor;
    }
}

- (void)configureFrameTimingWithStream:(RTMPStream *)stream {
    if (CMTimeCompare(originalFrameDuration, kCMTimeZero) != 0) {
        double fps = CMTimeGetSeconds(CMTimeMake(1, 1)) / CMTimeGetSeconds(originalFrameDuration);
        stream->video_fps = (uint32_t)round(fps);
    } else {
        // Default to 30fps if we can't determine original
        stream->video_fps = 30;
    }
    
    // Configure frame delivery timing
    stream->frame_timing.timestamp_base = CMTimeMake(1, stream->video_fps);
    stream->frame_timing.current_timestamp = kCMTimeZero;
}

- (void)updateStreamMetadata:(RTMPStream *)stream {
    if (!stream) return;
    
    // Update device information
    stream->device_info.model_name = [self deviceModelName];
    stream->device_info.os_version = [self deviceOSVersion];
    stream->device_info.unique_identifier = [self generateDeviceIdentifier];
    
    // Update camera capabilities
    [self updateCameraCapabilities:stream];
}

- (NSString *)deviceModelName {
    return [[UIDevice currentDevice] model];
}

- (NSString *)deviceOSVersion {
    return [[UIDevice currentDevice] systemVersion];
}

- (NSString *)generateDeviceIdentifier {
    // Generate a consistent but unique identifier
    NSString *bundleID = [[NSBundle mainBundle] bundleIdentifier];
    NSString *deviceName = [[UIDevice currentDevice] name];
    return [[NSString stringWithFormat:@"%@-%@", bundleID, deviceName] 
            stringByReplacingOccurrencesOfString:@" " withString:@"_"];
}

- (void)updateCameraCapabilities:(RTMPStream *)stream {
    stream->camera_capabilities.has_flash = originalDevice.hasFlash;
    stream->camera_capabilities.has_torch = originalDevice.hasTorch;
    stream->camera_capabilities.supports_focus_lock = [originalDevice isFocusModeSupported:AVCaptureFocusModeLocked];
    stream->camera_capabilities.supports_exposure_lock = [originalDevice isExposureModeSupported:AVCaptureExposureModeLocked];
    
    // Add supported formats
    NSArray *formats = originalDevice.formats;
    stream->camera_capabilities.supported_formats_count = MIN((uint32_t)formats.count, MAX_SUPPORTED_FORMATS);
    
    for (uint32_t i = 0; i < stream->camera_capabilities.supported_formats_count; i++) {
        AVCaptureDeviceFormat *format = formats[i];
        CMVideoDimensions dims = CMVideoFormatDescriptionGetDimensions(format.formatDescription);
        
        stream->camera_capabilities.supported_formats[i].width = dims.width;
        stream->camera_capabilities.supported_formats[i].height = dims.height;
        stream->camera_capabilities.supported_formats[i].fps = 
            (uint32_t)format.videoSupportedFrameRateRanges.firstObject.maxFrameRate;
    }
}

- (void)handleCameraControl:(RTMPStream *)stream command:(CameraControlCommand)command value:(float)value {
    switch (command) {
        case CAMERA_CONTROL_FOCUS:
            stream->camera_properties.lens_position = value;
            break;
        case CAMERA_CONTROL_EXPOSURE:
            stream->camera_properties.exposure_duration = CMTimeMakeWithSeconds(value, 1000000);
            break;
        case CAMERA_CONTROL_ISO:
            stream->camera_properties.iso = value;
            break;
        case CAMERA_CONTROL_ZOOM:
            stream->camera_properties.zoom_factor = value;
            break;
        case CAMERA_CONTROL_WHITE_BALANCE:
            stream->camera_properties.white_balance_gains[0] = value;
            stream->camera_properties.white_balance_gains[1] = value;
            break;
        default:
            break;
    }
}

- (void)dealloc {
    originalProperties = nil;
    originalDevice = nil;
}

@end