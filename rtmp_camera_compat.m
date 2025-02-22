#import "rtmp_camera_compat.h"
#import "rtmp_preview.h"
#import <objc/runtime.h>
#import <AVFoundation/AVFoundation.h>

// Private API hooks
@interface AVCaptureDevice (Private)
- (void)_setTransportControlsPresented:(BOOL)presented;
@end

@interface CAMCaptureEngine : NSObject
- (void)setDelegate:(id)delegate;
- (void)startCapture;
- (void)stopCapture;
@end

@interface RTMPCameraController () {
    AVCaptureSession *_captureSession;
    AVCaptureDeviceInput *_deviceInput;
    AVCaptureVideoDataOutput *_videoOutput;
    dispatch_queue_t _captureQueue;
    RTMPPreviewController *_previewController;
    
    CMTime _lastFrameTime;
    float _originalISO;
    CGFloat _originalZoomFactor;
    AVCaptureWhiteBalanceGains _originalWBGains;
    CMTime _originalExposureDuration;
    CGPoint _originalFocusPoint;
    
    BOOL _isStreaming;
    BOOL _isUsingRTMP;
    NSMutableDictionary *_cameraSettings;
}

@property (nonatomic, strong) NSMutableDictionary *streamMetrics;
@end

@implementation RTMPCameraController

+ (instancetype)sharedInstance {
    static RTMPCameraController *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[self alloc] init];
    });
    return instance;
}

- (id)init {
    self = [super init];
    if (self) {
        _captureQueue = dispatch_queue_create("com.rtmp.camera", DISPATCH_QUEUE_SERIAL);
        _streamMetrics = [NSMutableDictionary dictionary];
        _cameraSettings = [NSMutableDictionary dictionary];
        _previewController = [[RTMPPreviewController alloc] init];
        [self setupCaptureSession];
        [self setupMethodSwizzling];
    }
    return self;
}

- (void)setupCaptureSession {
    _captureSession = [[AVCaptureSession alloc] init];
    [_captureSession setSessionPreset:AVCaptureSessionPresetHigh];
    
    // Setup video output
    _videoOutput = [[AVCaptureVideoDataOutput alloc] init];
    _videoOutput.videoSettings = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)
    };
    [_videoOutput setSampleBufferDelegate:self queue:_captureQueue];
    
    if ([_captureSession canAddOutput:_videoOutput]) {
        [_captureSession addOutput:_videoOutput];
    }
    
    // Store original camera settings
    [self backupOriginalCameraSettings];
}

- (void)backupOriginalCameraSettings {
    AVCaptureDevice *device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (!device) return;
    
    _originalISO = device.ISO;
    _originalZoomFactor = device.videoZoomFactor;
    _originalWBGains = device.deviceWhiteBalanceGains;
    _originalExposureDuration = device.exposureDuration;
    _originalFocusPoint = device.focusPointOfInterest;
    
    // Store additional settings
    _cameraSettings[@"focusMode"] = @(device.focusMode);
    _cameraSettings[@"exposureMode"] = @(device.exposureMode);
    _cameraSettings[@"whiteBalanceMode"] = @(device.whiteBalanceMode);
}

- (void)setupMethodSwizzling {
    // Swizzle camera related methods
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        Class avCaptureDeviceClass = objc_getClass("AVCaptureDevice");
        
        // Swizzle device property methods
        [self swizzleMethod:@selector(ISO)
                withMethod:@selector(rtmp_ISO)
                  forClass:avCaptureDeviceClass];
        
        [self swizzleMethod:@selector(exposureDuration)
                withMethod:@selector(rtmp_exposureDuration)
                  forClass:avCaptureDeviceClass];
        
        [self swizzleMethod:@selector(focusPointOfInterest)
                withMethod:@selector(rtmp_focusPointOfInterest)
                  forClass:avCaptureDeviceClass];
    });
}

- (void)swizzleMethod:(SEL)originalSelector withMethod:(SEL)swizzledSelector forClass:(Class)class {
    Method originalMethod = class_getInstanceMethod(class, originalSelector);
    Method swizzledMethod = class_getInstanceMethod(class, swizzledSelector);
    
    BOOL didAddMethod = class_addMethod(class,
                                      originalSelector,
                                      method_getImplementation(swizzledMethod),
                                      method_getTypeEncoding(swizzledMethod));
    
    if (didAddMethod) {
        class_replaceMethod(class,
                          swizzledSelector,
                          method_getImplementation(originalMethod),
                          method_getTypeEncoding(originalMethod));
    } else {
        method_exchangeImplementations(originalMethod, swizzledMethod);
    }
}

#pragma mark - RTMP Control

- (void)startRTMPSession {
    if (_isStreaming) return;
    
    _isStreaming = YES;
    _isUsingRTMP = YES;
    
    dispatch_async(dispatch_get_main_queue(), ^{
        [self->_previewController showPreview];
    });
    
    // Start monitoring stream health
    [self startStreamMonitoring];
}

- (void)stopRTMPSession {
    _isStreaming = NO;
    _isUsingRTMP = NO;
    
    // Restore original camera settings
    [self restoreOriginalCameraSettings];
    
    dispatch_async(dispatch_get_main_queue(), ^{
        [self->_previewController hidePreview];
    });
    
    [self stopStreamMonitoring];
}

- (void)restoreOriginalCameraSettings {
    AVCaptureDevice *device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    if (!device) return;
    
    [device lockForConfiguration:nil];
    
    device.videoZoomFactor = _originalZoomFactor;
    
    if ([device isExposureModeSupported:AVCaptureExposureModeContinuousAutoExposure]) {
        device.exposureMode = AVCaptureExposureModeContinuousAutoExposure;
    }
    
    if ([device isFocusModeSupported:AVCaptureFocusModeContinuousAutoFocus]) {
        device.focusMode = AVCaptureFocusModeContinuousAutoFocus;
    }
    
    [device setExposureModeCustomWithDuration:_originalExposureDuration
                                         ISO:_originalISO
                           completionHandler:nil];
    
    [device setWhiteBalanceGains:_originalWBGains completionHandler:nil];
    
    [device unlockForConfiguration];
}

#pragma mark - Camera Parameter Emulation

// Swizzled methods
- (float)rtmp_ISO {
    if (_isUsingRTMP) {
        return [_cameraSettings[@"currentISO"] floatValue];
    }
    return [self rtmp_ISO];
}

- (CMTime)rtmp_exposureDuration {
    if (_isUsingRTMP) {
        return [_cameraSettings[@"currentExposureDuration"] CMTimeValue];
    }
    return [self rtmp_exposureDuration];
}

- (CGPoint)rtmp_focusPointOfInterest {
    if (_isUsingRTMP) {
        return [_cameraSettings[@"currentFocusPoint"] CGPointValue];
    }
    return [self rtmp_focusPointOfInterest];
}

#pragma mark - Stream Monitoring

- (void)startStreamMonitoring {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), ^{
        while (self->_isStreaming) {
            [self updateStreamMetrics];
            [NSThread sleepForTimeInterval:1.0];
        }
    });
}

- (void)updateStreamMetrics {
    // Calculate current stream metrics
    NSTimeInterval currentTime = [[NSDate date] timeIntervalSince1970];
    NSTimeInterval elapsed = currentTime - [self.streamMetrics[@"lastUpdate"] doubleValue];
    
    if (elapsed >= 1.0) {
        // Update metrics
        NSInteger frameCount = [self.streamMetrics[@"frameCount"] integerValue];
        float fps = frameCount / elapsed;
        
        self.streamMetrics[@"fps"] = @(fps);
        self.streamMetrics[@"frameCount"] = @(0);
        self.streamMetrics[@"lastUpdate"] = @(currentTime);
        
        // Update preview
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_previewController updateStreamMetrics:self.streamMetrics];
        });
    }
}

#pragma mark - AVCaptureVideoDataOutputSampleBufferDelegate

- (void)captureOutput:(AVCaptureOutput *)output
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection *)connection {
    if (!_isStreaming) return;
    
    // Process frame
    [self processVideoFrame:sampleBuffer];
    
    // Update preview if needed
    if (_isUsingRTMP) {
        [_previewController updateFrame:sampleBuffer];
    }
    
    // Update metrics
    NSInteger frameCount = [self.streamMetrics[@"frameCount"] integerValue];
    self.streamMetrics[@"frameCount"] = @(frameCount + 1);
}

- (void)processVideoFrame:(CMSampleBufferRef)sampleBuffer {
    if (!_isUsingRTMP) return;
    
    // Extract frame metadata
    CMTime presentationTime = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
    Float64 frameInterval = CMTimeGetSeconds(CMTimeSubtract(presentationTime, _lastFrameTime));
    _lastFrameTime = presentationTime;
    
    // Update camera settings based on RTMP stream
    [self updateCameraSettingsFromFrame:sampleBuffer];
    
    // Calculate frame metrics
    float instantFPS = frameInterval > 0 ? 1.0 / frameInterval : 0;
    self.streamMetrics[@"instantFPS"] = @(instantFPS);
}

- (void)updateCameraSettingsFromFrame:(CMSampleBufferRef)sampleBuffer {
    // Extract frame metadata
    CFDictionaryRef metadataDict = CMGetAttachment(sampleBuffer, kCGImagePropertyExifDictionary, NULL);
    if (metadataDict) {
        CFNumberRef isoNumber = CFDictionaryGetValue(metadataDict, kCGImagePropertyExifISOSpeedRatings);
        if (isoNumber) {
            float iso;
            CFNumberGetValue(isoNumber, kCFNumberFloatType, &iso);
            _cameraSettings[@"currentISO"] = @(iso);
        }
        
        CFNumberRef exposureTimeNumber = CFDictionaryGetValue(metadataDict, kCGImagePropertyExifExposureTime);
        if (exposureTimeNumber) {
            Float64 exposureTime;
            CFNumberGetValue(exposureTimeNumber, kCFNumberFloat64Type, &exposureTime);
            CMTime exposureDuration = CMTimeMakeWithSeconds(exposureTime, 1000000000);
            _cameraSettings[@"currentExposureDuration"] = [NSValue valueWithCMTime:exposureDuration];
        }
    }
}

@end