#import "rtmp_camera_compat.h"
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

@implementation RTMPCameraCompat {
    AVCaptureDevice *originalDevice;
    NSDictionary *originalProperties;
    CMTime originalFrameDuration;
    AVCaptureDeviceFormat *originalFormat;
    AVCaptureDeviceInput *virtualInput;
    NSString *originalDeviceID;
    NSString *originalModelID;
    NSString *originalLocalizedName;
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
    
    // Store original device identifiers
    originalDeviceID = [originalDevice.uniqueID copy];
    originalModelID = [originalDevice.modelID copy];
    originalLocalizedName = [originalDevice.localizedName copy];
    
    // Store full device configuration
    originalProperties = @{
        // Basic properties
        @"uniqueID": originalDevice.uniqueID,
        @"modelID": originalDevice.modelID,
        @"localizedName": originalDevice.localizedName,
        
        // Camera capabilities
        @"hasFlash": @(originalDevice.hasFlash),
        @"hasTorch": @(originalDevice.hasTorch),
        @"flashAvailable": @(originalDevice.flashAvailable),
        @"torchAvailable": @(originalDevice.torchAvailable),
        @"focusPointOfInterestSupported": @(originalDevice.focusPointOfInterestSupported),
        @"exposurePointOfInterestSupported": @(originalDevice.exposurePointOfInterestSupported),
        
        // Current settings
        @"exposureMode": @(originalDevice.exposureMode),
        @"whiteBalanceMode": @(originalDevice.whiteBalanceMode),
        @"focusMode": @(originalDevice.focusMode),
        @"iso": @(originalDevice.iso),
        @"exposureDuration": [NSValue valueWithCMTime:originalDevice.exposureDuration],
        @"deviceWhiteBalanceGains": [NSValue valueWithCGPoint:CGPointMake(
            originalDevice.deviceWhiteBalanceGains.redGain,
            originalDevice.deviceWhiteBalanceGains.greenGain)],
        @"lensPosition": @(originalDevice.lensPosition),
        @"videoZoomFactor": @(originalDevice.videoZoomFactor),
        
        // Ranges and limits
        @"activeFormat": originalDevice.activeFormat,
        @"activeVideoMinFrameDuration": [NSValue valueWithCMTime:originalDevice.activeVideoMinFrameDuration],
        @"activeVideoMaxFrameDuration": [NSValue valueWithCMTime:originalDevice.activeVideoMaxFrameDuration],
        @"minExposureDuration": [NSValue valueWithCMTime:originalDevice.minExposureDuration],
        @"maxExposureDuration": [NSValue valueWithCMTime:originalDevice.maxExposureDuration],
        @"minISO": @(originalDevice.minISO),
        @"maxISO": @(originalDevice.maxISO),
        @"videoZoomFactorUpscaleThreshold": @(originalDevice.videoZoomFactorUpscaleThreshold),
        @"maxWhiteBalanceGain": @(originalDevice.maxWhiteBalanceGain),
    };
    
    originalFormat = originalDevice.activeFormat;
    originalFrameDuration = originalDevice.activeVideoMinFrameDuration;
}

- (AVCaptureDeviceInput *)createVirtualDeviceInput {
    // Create a virtual camera device that perfectly mimics the original
    Class virtualDeviceClass = NSClassFromString(@"AVCaptureDeviceVirtual");
    id virtualDevice = [[virtualDeviceClass alloc] init];
    
    // Set all basic properties
    [virtualDevice setValue:originalDeviceID forKey:@"uniqueID"];
    [virtualDevice setValue:originalModelID forKey:@"modelID"];
    [virtualDevice setValue:originalLocalizedName forKey:@"localizedName"];
    
    // Set capabilities
    [virtualDevice setValue:originalProperties[@"hasFlash"] forKey:@"hasFlash"];
    [virtualDevice setValue:originalProperties[@"hasTorch"] forKey:@"hasTorch"];
    [virtualDevice setValue:originalProperties[@"flashAvailable"] forKey:@"flashAvailable"];
    [virtualDevice setValue:originalProperties[@"torchAvailable"] forKey:@"torchAvailable"];
    [virtualDevice setValue:originalProperties[@"focusPointOfInterestSupported"] forKey:@"focusPointOfInterestSupported"];
    [virtualDevice setValue:originalProperties[@"exposurePointOfInterestSupported"] forKey:@"exposurePointOfInterestSupported"];
    
    // Configure format and ranges
    [virtualDevice setValue:originalFormat forKey:@"activeFormat"];
    [virtualDevice setValue:originalProperties[@"activeVideoMinFrameDuration"] forKey:@"activeVideoMinFrameDuration"];
    [virtualDevice setValue:originalProperties[@"activeVideoMaxFrameDuration"] forKey:@"activeVideoMaxFrameDuration"];
    [virtualDevice setValue:originalProperties[@"minExposureDuration"] forKey:@"minExposureDuration"];
    [virtualDevice setValue:originalProperties[@"maxExposureDuration"] forKey:@"maxExposureDuration"];
    [virtualDevice setValue:originalProperties[@"minISO"] forKey:@"minISO"];
    [virtualDevice setValue:originalProperties[@"maxISO"] forKey:@"maxISO"];
    
    // Create input
    NSError *error = nil;
    virtualInput = [[AVCaptureDeviceInput alloc] initWithDevice:virtualDevice error:&error];
    
    if (error) {
        NSLog(@"[RTMPCamera] Failed to create virtual device input: %@", error);
        return nil;
    }
    
    return virtualInput;
}

- (void)configureVirtualCameraWithStream:(RTMPStream *)stream {
    if (!stream) return;
    
    // Configure video format to match original camera
    stream->video_width = (uint32_t)CMVideoFormatDescriptionGetDimensions(originalFormat.formatDescription).width;
    stream->video_height = (uint32_t)CMVideoFormatDescriptionGetDimensions(originalFormat.formatDescription).height;
    stream->video_fps = (uint32_t)(CMTimeGetSeconds(CMTimeMake(1, 1)) / CMTimeGetSeconds(originalFrameDuration));
    
    // Set camera properties
    stream->camera_properties.exposure_mode = [originalProperties[@"exposureMode"] intValue];
    stream->camera_properties.exposure_duration = [[originalProperties[@"exposureDuration"] CMTimeValue] timeValue];
    stream->camera_properties.iso = [originalProperties[@"iso"] floatValue];
    stream->camera_properties.white_balance_mode = [originalProperties[@"whiteBalanceMode"] intValue];
    
    CGPoint whiteBalanceGains = [[originalProperties[@"deviceWhiteBalanceGains"] CGPointValue];
    stream->camera_properties.white_balance_gains[0] = whiteBalanceGains.x;
    stream->camera_properties.white_balance_gains[1] = whiteBalanceGains.y;
    
    stream->camera_properties.focus_mode = [originalProperties[@"focusMode"] intValue];
    stream->camera_properties.lens_position = [originalProperties[@"lensPosition"] floatValue];
    stream->camera_properties.zoom_factor = [originalProperties[@"videoZoomFactor"] floatValue];
    
    if ([originalFormat respondsToSelector:@selector(videoFieldOfView)]) {
        stream->camera_properties.field_of_view = originalFormat.videoFieldOfView;
    }
}

- (void)updateStreamMetadata:(RTMPStream *)stream {
    if (!stream) return;
    
    // Update device information
    stream->device_info.model_name = [originalModelID UTF8String];
    stream->device_info.unique_identifier = [originalDeviceID UTF8String];
    
    // Update camera capabilities
    stream->camera_capabilities.has_flash = [originalProperties[@"hasFlash"] boolValue];
    stream->camera_capabilities.has_torch = [originalProperties[@"hasTorch"] boolValue];
    stream->camera_capabilities.supports_focus_lock = true;
    stream->camera_capabilities.supports_exposure_lock = true;
    
    // Get supported formats
    NSArray *formats = [originalDevice formats];
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

- (void)dealloc {
    originalProperties = nil;
    originalDevice = nil;
    virtualInput = nil;
}

@end