#import "rtmp_camera_compat.h"
#import <objc/runtime.h>
#import <libkern/OSAtomic.h>

// Static variables
static RTMPCameraCompatibility *gSharedInstance = nil;
static const char *kRTMPFrameQueueKey = "RTMPFrameQueue";
static volatile int32_t gIsPublishing = 0;

// Private interface
@interface RTMPCameraCompatibility () <AVCaptureVideoDataOutputSampleBufferDelegate>

@property (nonatomic, strong) AVCaptureSession *captureSession;
@property (nonatomic, strong) AVCaptureDevice *device;
@property (nonatomic, strong) AVCaptureDeviceInput *input;
@property (nonatomic, strong) AVCaptureVideoDataOutput *output;
@property (nonatomic, strong) AVCaptureVideoPreviewLayer *previewLayer;
@property (nonatomic, strong) dispatch_queue_t videoQueue;
@property (nonatomic, strong) dispatch_queue_t processingQueue;
@property (nonatomic, strong) NSMutableArray *frameQueue;
@property (nonatomic, strong) RTMPCameraStats *stats;
@property (nonatomic, strong) RTMPCameraSettings *settings;
@property (nonatomic, assign) RTMPCameraState state;
@property (nonatomic, assign) CVPixelBufferRef lastFrame;
@property (nonatomic, strong) NSDate *startTime;
@property (nonatomic, assign) BOOL isPreviewRunning;
@property (nonatomic, assign) dispatch_semaphore_t frameLock;

@end

@implementation RTMPCameraSettings

+ (instancetype)defaultSettings {
    RTMPCameraSettings *settings = [[RTMPCameraSettings alloc] init];
    settings.resolution = CGSizeMake(1280, 720);
    settings.frameRate = 30.0f;
    settings.position = AVCaptureDevicePositionBack;
    settings.autoFocus = YES;
    settings.autoExposure = YES;
    settings.autoWhiteBalance = YES;
    settings.zoom = 1.0f;
    settings.focusPoint = 0.5f;
    settings.exposurePoint = CGPointMake(0.5f, 0.5f);
    return settings;
}

- (BOOL)isEqual:(RTMPCameraSettings *)other {
    if (![other isKindOfClass:[RTMPCameraSettings class]]) return NO;
    
    return CGSizeEqualToSize(self.resolution, other.resolution) &&
           fabs(self.frameRate - other.frameRate) < 0.1f &&
           self.position == other.position &&
           self.autoFocus == other.autoFocus &&
           self.autoExposure == other.autoExposure &&
           self.autoWhiteBalance == other.autoWhiteBalance &&
           fabs(self.zoom - other.zoom) < 0.01f &&
           fabs(self.focusPoint - other.focusPoint) < 0.01f &&
           CGPointEqualToPoint(self.exposurePoint, other.exposurePoint);
}

- (NSDictionary *)serialize {
    return @{
        @"width": @(self.resolution.width),
        @"height": @(self.resolution.height),
        @"frameRate": @(self.frameRate),
        @"position": @(self.position),
        @"autoFocus": @(self.autoFocus),
        @"autoExposure": @(self.autoExposure),
        @"autoWhiteBalance": @(self.autoWhiteBalance),
        @"zoom": @(self.zoom),
        @"focusPoint": @(self.focusPoint),
        @"exposureX": @(self.exposurePoint.x),
        @"exposureY": @(self.exposurePoint.y)
    };
}

+ (instancetype)settingsWithDictionary:(NSDictionary *)dict {
    RTMPCameraSettings *settings = [[RTMPCameraSettings alloc] init];
    settings.resolution = CGSizeMake([dict[@"width"] floatValue], 
                                   [dict[@"height"] floatValue]);
    settings.frameRate = [dict[@"frameRate"] floatValue];
    settings.position = [dict[@"position"] intValue];
    settings.autoFocus = [dict[@"autoFocus"] boolValue];
    settings.autoExposure = [dict[@"autoExposure"] boolValue];
    settings.autoWhiteBalance = [dict[@"autoWhiteBalance"] boolValue];
    settings.zoom = [dict[@"zoom"] floatValue];
    settings.focusPoint = [dict[@"focusPoint"] floatValue];
    settings.exposurePoint = CGPointMake([dict[@"exposureX"] floatValue],
                                       [dict[@"exposureY"] floatValue]);
    return settings;
}

@end

@implementation RTMPCameraStats

- (instancetype)init {
    if (self = [super init]) {
        _resolution = CGSizeZero;
        _frameRate = 0;
        _currentFPS = 0;
        _frameCount = 0;
        _droppedFrames = 0;
        _totalBytes = 0;
        _bitrate = 0;
        _uptime = 0;
        _hasVideo = NO;
        _hasAudio = NO;
        _isPublishing = NO;
    }
    return self;
}

- (NSDictionary *)serialize {
    return @{
        @"width": @(self.resolution.width),
        @"height": @(self.resolution.height),
        @"frameRate": @(self.frameRate),
        @"currentFPS": @(self.currentFPS),
        @"frameCount": @(self.frameCount),
        @"droppedFrames": @(self.droppedFrames),
        @"totalBytes": @(self.totalBytes),
        @"bitrate": @(self.bitrate),
        @"uptime": @(self.uptime),
        @"hasVideo": @(self.hasVideo),
        @"hasAudio": @(self.hasAudio),
        @"isPublishing": @(self.isPublishing)
    };
}

+ (instancetype)statsWithDictionary:(NSDictionary *)dict {
    RTMPCameraStats *stats = [[RTMPCameraStats alloc] init];
    stats.resolution = CGSizeMake([dict[@"width"] floatValue],
                                [dict[@"height"] floatValue]);
    stats.frameRate = [dict[@"frameRate"] floatValue];
    stats.currentFPS = [dict[@"currentFPS"] floatValue];
    stats.frameCount = [dict[@"frameCount"] unsignedLongLongValue];
    stats.droppedFrames = [dict[@"droppedFrames"] unsignedLongLongValue];
    stats.totalBytes = [dict[@"totalBytes"] unsignedLongLongValue];
    stats.bitrate = [dict[@"bitrate"] floatValue];
    stats.uptime = [dict[@"uptime"] doubleValue];
    stats.hasVideo = [dict[@"hasVideo"] boolValue];
    stats.hasAudio = [dict[@"hasAudio"] boolValue];
    stats.isPublishing = [dict[@"isPublishing"] boolValue];
    return stats;
}

@end

@implementation RTMPCameraCompatibility

#pragma mark - Initialization & Lifecycle

+ (instancetype)sharedInstance {
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        gSharedInstance = [[RTMPCameraCompatibility alloc] init];
    });
    return gSharedInstance;
}

- (instancetype)init {
    if (self = [super init]) {
        _state = RTMPCameraStateOff;
        _stats = [[RTMPCameraStats alloc] init];
        _settings = [RTMPCameraSettings defaultSettings];
        _frameQueue = [NSMutableArray array];
        _videoQueue = dispatch_queue_create("com.rtmpcamera.video", DISPATCH_QUEUE_SERIAL);
        _processingQueue = dispatch_queue_create("com.rtmpcamera.processing", DISPATCH_QUEUE_SERIAL);
        _frameLock = dispatch_semaphore_create(1);
        
        // Register for app lifecycle notifications
        [[NSNotificationCenter defaultCenter] addObserver:self
                                               selector:@selector(applicationWillResignActive:)
                                                   name:UIApplicationWillResignActiveNotification
                                                 object:nil];
        
        [[NSNotificationCenter defaultCenter] addObserver:self
                                               selector:@selector(applicationDidBecomeActive:)
                                                   name:UIApplicationDidBecomeActiveNotification
                                                 object:nil];
    }
    return self;
}

- (void)dealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [self cleanup];
}

#pragma mark - Camera Control

- (void)startWithSettings:(RTMPCameraSettings *)settings {
    if (_state != RTMPCameraStateOff) return;
    
    _settings = settings ?: [RTMPCameraSettings defaultSettings];
    [self setupCaptureSession];
}

- (void)stop {
    if (_state == RTMPCameraStateOff) return;
    
    _state = RTMPCameraStateStopping;
    [self cleanup];
    _state = RTMPCameraStateOff;
    
    if ([_delegate respondsToSelector:@selector(cameraStateDidChange:)]) {
        [_delegate cameraStateDidChange:_state];
    }
}

- (void)restart {
    RTMPCameraSettings *currentSettings = _settings;
    [self stop];
    [self startWithSettings:currentSettings];
}

- (BOOL)isRunning {
    return _state == RTMPCameraStateRunning;
}

#pragma mark - Setup & Configuration

- (void)setupCaptureSession {
    _state = RTMPCameraStateStarting;
    if ([_delegate respondsToSelector:@selector(cameraStateDidChange:)]) {
        [_delegate cameraStateDidChange:_state];
    }
    
    dispatch_async(_videoQueue, ^{
        NSError *error = nil;
        
        // Create capture session
        _captureSession = [[AVCaptureSession alloc] init];
        
        // Configure resolution
        if (_settings.resolution.width >= 1920) {
            [_captureSession setSessionPreset:AVCaptureSessionPreset1920x1080];
        } else if (_settings.resolution.width >= 1280) {
            [_captureSession setSessionPreset:AVCaptureSessionPreset1280x720];
        } else if (_settings.resolution.width >= 640) {
            [_captureSession setSessionPreset:AVCaptureSessionPreset640x480];
        } else {
            [_captureSession setSessionPreset:AVCaptureSessionPresetLow];
        }
        
        // Get camera device
        _device = [self cameraWithPosition:_settings.position];
        if (!_device) {
            error = [NSError errorWithDomain:@"RTMPCamera" 
                                      code:-1
                                  userInfo:@{NSLocalizedDescriptionKey: @"Failed to get camera device"}];
            [self handleSetupError:error];
            return;
        }
        
        // Create device input
        _input = [AVCaptureDeviceInput deviceInputWithDevice:_device error:&error];
        if (!_input || error) {
            [self handleSetupError:error];
            return;
        }
        
        if ([_captureSession canAddInput:_input]) {
            [_captureSession addInput:_input];
        } else {
            error = [NSError errorWithDomain:@"RTMPCamera"
                                      code:-2 
                                  userInfo:@{NSLocalizedDescriptionKey: @"Failed to add camera input"}];
            [self handleSetupError:error];
            return;
        }
        
        // Create and configure video output
        _output = [[AVCaptureVideoDataOutput alloc] init];
        _output.videoSettings = @{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
            (id)kCVPixelBufferWidthKey: @(_settings.resolution.width),
            (id)kCVPixelBufferHeightKey: @(_settings.resolution.height)
        };
        _output.alwaysDiscardsLateVideoFrames = YES;
        [_output setSampleBufferDelegate:self queue:_videoQueue];
        
        if ([_captureSession canAddOutput:_output]) {
            [_captureSession addOutput:_output];
        } else {
            error = [NSError errorWithDomain:@"RTMPCamera"
                                      code:-3
                                  userInfo:@{NSLocalizedDescriptionKey: @"Failed to add video output"}];
            [self handleSetupError:error];
            return;
        }
        
        // Configure camera settings
        [self configureCameraWithError:&error];
        if (error) {
            [self handleSetupError:error];
            return;
        }
        
        // Start capture session
        [_captureSession startRunning];
        
        // Update state
        _state = RTMPCameraStateRunning;
        _startTime = [NSDate date];
        
        dispatch_async(dispatch_get_main_queue(), ^{
            if ([_delegate respondsToSelector:@selector(cameraStateDidChange:)]) {
                [_delegate cameraStateDidChange:_state];
            }
        });
    });
}

- (void)configureCameraWithError:(NSError **)error {
    if ([_device lockForConfiguration:error]) {
        // Configure frame rate
        CMTime frameDuration = CMTimeMake(1, (int32_t)_settings.frameRate);
        _device.activeVideoMinFrameDuration = frameDuration;
        _device.activeVideoMaxFrameDuration = frameDuration;
        
        // Configure focus
        if ([_device isFocusModeSupported:AVCaptureFocusModeContinuousAutoFocus]) {
            _device.focusMode = _settings.autoFocus ? 
                AVCaptureFocusModeContinuousAutoFocus : AVCaptureFocusModeAutoFocus;
        }
        
        // Configure exposure
        if ([_device isExposureModeSupported:AVCaptureExposureModeContinuousAutoExposure]) {
            _device.exposureMode = _settings.autoExposure ? 
                AVCaptureExposureModeContinuousAutoExposure : AVCaptureExposureModeAutoExpose;
        }
        
        // Configure white balance
        if ([_device isWhiteBalanceModeSupported:AVCaptureWhiteBalanceModeContinuousAutoWhiteBalance]) {
            _device.whiteBalanceMode = _settings.autoWhiteBalance ? 
                AVCaptureWhiteBalanceModeContinuousAutoWhiteBalance : AVCaptureWhiteBalanceModeAutoWhiteBalance;
        }
        
        // Configure zoom
        if (_device.videoZoomFactor != _settings.zoom) {
            _device.videoZoomFactor = MIN(_settings.zoom, _device.activeFormat.videoMaxZoomFactor);
        }
        
        [_device unlockForConfiguration];
    }
}

- (void)cleanup {
    // Stop capture session
    [_captureSession stopRunning];
    
    // Remove inputs and outputs
    for (AVCaptureInput *input in _captureSession.inputs) {
        [_captureSession removeInput:input];
    }
    
    for (AVCaptureOutput *output in _captureSession.outputs) {
        [_captureSession removeOutput:output];
    }

    // Release objects
    _captureSession = nil;
    _device = nil;
    _input = nil;
    _output = nil;
    
    // Clear frame queue
    [_frameQueue removeAllObjects];
    
    // Release last frame
    if (_lastFrame) {
        CVPixelBufferRelease(_lastFrame);
        _lastFrame = NULL;
    }
    
    // Reset stats
    _stats = [[RTMPCameraStats alloc] init];
    
    // Stop preview if running
    if (_isPreviewRunning) {
        [self stopPreview];
    }
}

#pragma mark - Frame Processing

- (void)processRTMPFrame:(void*)frameData 
                   size:(size_t)frameSize 
              timestamp:(uint32_t)timestamp 
            isKeyframe:(BOOL)isKeyframe {
    
    if (_state != RTMPCameraStateRunning) return;
    
    dispatch_async(_processingQueue, ^{
        // Update stats
        _stats.hasVideo = YES;
        _stats.totalBytes += frameSize;
        _stats.frameCount++;
        _stats.isPublishing = OSAtomicOr32(0, &gIsPublishing);
        
        // Calculate bitrate (bytes per second)
        NSTimeInterval uptime = -[_startTime timeIntervalSinceNow];
        _stats.uptime = uptime;
        _stats.bitrate = (_stats.totalBytes * 8.0f) / (uptime * 1024.0f); // kbps
        
        // Convert RTMP frame to CVPixelBuffer
        CVPixelBufferRef pixelBuffer = [self createPixelBufferFromRTMPFrame:frameData 
                                                                      size:frameSize];
        if (!pixelBuffer) {
            _stats.droppedFrames++;
            return;
        }
        
        // Store last frame
        dispatch_semaphore_wait(_frameLock, DISPATCH_TIME_FOREVER);
        if (_lastFrame) {
            CVPixelBufferRelease(_lastFrame);
        }
        _lastFrame = pixelBuffer;
        dispatch_semaphore_signal(_frameLock);
        
        // Update stats
        _stats.currentFPS = _stats.frameCount / uptime;
        
        // Notify delegate
        dispatch_async(dispatch_get_main_queue(), ^{
            if ([_delegate respondsToSelector:@selector(cameraDidUpdateStats:)]) {
                [_delegate cameraDidUpdateStats:_stats];
            }
        });
    });
}

- (CVPixelBufferRef)createPixelBufferFromRTMPFrame:(void*)frameData size:(size_t)frameSize {
    if (!frameData || frameSize == 0) return NULL;
    
    // Create pixel buffer
    CVPixelBufferRef pixelBuffer = NULL;
    NSDictionary *options = @{
        (NSString*)kCVPixelBufferCGImageCompatibilityKey: @YES,
        (NSString*)kCVPixelBufferCGBitmapContextCompatibilityKey: @YES,
    };
    
    CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault,
                                         _settings.resolution.width,
                                         _settings.resolution.height,
                                         kCVPixelFormatType_32BGRA,
                                         (__bridge CFDictionaryRef)options,
                                         &pixelBuffer);
    
    if (status != kCVReturnSuccess) {
        return NULL;
    }
    
    // Lock buffer for writing
    CVPixelBufferLockBaseAddress(pixelBuffer, 0);
    
    // Copy frame data
    void *baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
    size_t height = CVPixelBufferGetHeight(pixelBuffer);
    
    // Ensure buffer has enough space
    size_t maxSize = bytesPerRow * height;
    if (frameSize > maxSize) {
        CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
        CVPixelBufferRelease(pixelBuffer);
        return NULL;
    }
    
    memcpy(baseAddress, frameData, frameSize);
    
    // Unlock buffer
    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    
    return pixelBuffer;
}

- (void)flushBuffers {
    dispatch_async(_processingQueue, ^{
        dispatch_semaphore_wait(_frameLock, DISPATCH_TIME_FOREVER);
        
        if (_lastFrame) {
            CVPixelBufferRelease(_lastFrame);
            _lastFrame = NULL;
        }
        
        [_frameQueue removeAllObjects];
        
        dispatch_semaphore_signal(_frameLock);
    });
}

- (CVPixelBufferRef)copyLastFrame {
    __block CVPixelBufferRef frame = NULL;
    
    dispatch_semaphore_wait(_frameLock, DISPATCH_TIME_FOREVER);
    if (_lastFrame) {
        CVPixelBufferRetain(_lastFrame);
        frame = _lastFrame;
    }
    dispatch_semaphore_signal(_frameLock);
    
    return frame;
}

#pragma mark - Preview

- (UIView *)previewView {
    if (!_previewLayer) {
        UIView *view = [[UIView alloc] initWithFrame:CGRectZero];
        view.backgroundColor = [UIColor blackColor];
        
        _previewLayer = [[AVCaptureVideoPreviewLayer alloc] initWithSession:_captureSession];
        _previewLayer.videoGravity = AVLayerVideoGravityResizeAspectFill;
        _previewLayer.frame = view.bounds;
        
        [view.layer addSublayer:_previewLayer];
        
        view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        
        _isPreviewRunning = NO;
    }
    
    return _previewLayer.superview;
}

- (void)startPreview {
    if (!_isPreviewRunning && _state == RTMPCameraStateRunning) {
        [_captureSession startRunning];
        _isPreviewRunning = YES;
    }
}

- (void)stopPreview {
    if (_isPreviewRunning) {
        [_captureSession stopRunning];
        _isPreviewRunning = NO;
    }
}

- (void)updatePreviewOrientation {
    if (!_previewLayer) return;
    
    UIInterfaceOrientation orientation = [[UIApplication sharedApplication] statusBarOrientation];
    
    _previewLayer.connection.videoOrientation = (AVCaptureVideoOrientation)orientation;
}

#pragma mark - Utilities

- (NSArray<NSValue *> *)supportedResolutions {
    NSMutableArray *resolutions = [NSMutableArray array];
    
    AVCaptureDevice *device = [self cameraWithPosition:_settings.position];
    if (!device) return resolutions;
    
    for (AVCaptureDeviceFormat *format in device.formats) {
        CMVideoFormatDescriptionRef desc = format.formatDescription;
        CGSize dimensions = CMVideoFormatDescriptionGetDimensions(desc);
        [resolutions addObject:[NSValue valueWithCGSize:dimensions]];
    }
    
    return resolutions;
}

- (NSArray<NSNumber *> *)supportedFrameRates {
    NSMutableArray *frameRates = [NSMutableArray array];
    
    AVCaptureDevice *device = [self cameraWithPosition:_settings.position];
    if (!device) return frameRates;
    
    for (AVFrameRateRange *range in device.activeFormat.videoSupportedFrameRateRanges) {
        [frameRates addObject:@(range.maxFrameRate)];
    }
    
    return frameRates;
}

- (BOOL)supportsCamera:(AVCaptureDevicePosition)position {
    return [self cameraWithPosition:position] != nil;
}

- (BOOL)hasMultipleCameras {
    return [[AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo] count] > 1;
}

- (AVCaptureDevice *)currentDevice {
    return _device;
}

#pragma mark - Private Methods

- (AVCaptureDevice *)cameraWithPosition:(AVCaptureDevicePosition)position {
    NSArray *devices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo];
    for (AVCaptureDevice *device in devices) {
        if (device.position == position) {
            return device;
        }
    }
    return nil;
}

- (void)handleSetupError:(NSError *)error {
    // Cleanup any partially initialized objects
    [self cleanup];
    
    // Update state
    _state = RTMPCameraStateError;
    
    // Notify delegate
    dispatch_async(dispatch_get_main_queue(), ^{
        if ([_delegate respondsToSelector:@selector(cameraStateDidChange:)]) {
            [_delegate cameraStateDidChange:_state];
        }
        
        if ([_delegate respondsToSelector:@selector(cameraDidEncounterError:)]) {
            [_delegate cameraDidEncounterError:error];
        }
    });
}

#pragma mark - App Lifecycle

- (void)applicationWillResignActive:(NSNotification *)notification {
    if (_state == RTMPCameraStateRunning) {
        [self stopPreview];
    }
}

- (void)applicationDidBecomeActive:(NSNotification *)notification {
    if (_state == RTMPCameraStateRunning && _isPreviewRunning) {
        [self startPreview];
    }
}

#pragma mark - AVCaptureVideoDataOutputSampleBufferDelegate

- (void)captureOutput:(AVCaptureOutput *)output 
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer 
       fromConnection:(AVCaptureConnection *)connection {
    
    if (_state != RTMPCameraStateRunning) return;
    
    // Get image buffer
    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (!imageBuffer) return;
    
    // Lock buffer
    CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
    
    // Get buffer info
    void *baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
    size_t height = CVPixelBufferGetHeight(imageBuffer);
    size_t size = bytesPerRow * height;
    
    // Copy to new buffer to avoid threading issues
    void *frameData = malloc(size);
    if (frameData) {
        memcpy(frameData, baseAddress, size);
        
        // Add to frame queue
        [_frameQueue addObject:[NSData dataWithBytesNoCopy:frameData 
                                                   length:size 
                                             freeWhenDone:YES]];
    }
    
    // Unlock buffer
    CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
}

@end

#pragma mark - C Interface

bool rtmp_camera_compat_initialize(void) {
    @autoreleasepool {
        return [RTMPCameraCompatibility sharedInstance] != nil;
    }
}

void rtmp_camera_compat_cleanup(void) {
    @autoreleasepool {
        [[RTMPCameraCompatibility sharedInstance] stop];
    }
}

void rtmp_camera_compat_start(void) {
    @autoreleasepool {
        [[RTMPCameraCompatibility sharedInstance] startWithSettings:nil];
    }
}

void rtmp_camera_compat_stop(void) {
    @autoreleasepool {
        [[RTMPCameraCompatibility sharedInstance] stop];
    }
}

bool rtmp_camera_compat_is_running(void) {
    @autoreleasepool {
        return [[RTMPCameraCompatibility sharedInstance] isRunning];
    }
}

void rtmp_camera_compat_process_frame(void* frame_data, 
                                    size_t frame_size,
                                    uint32_t timestamp,
                                    bool is_keyframe) {
    @autoreleasepool {
        [[RTMPCameraCompatibility sharedInstance] processRTMPFrame:frame_data
                                                            size:frame_size
                                                       timestamp:timestamp
                                                     isKeyframe:is_keyframe];
    }
}

bool rtmp_camera_compat_get_resolution(int* width, int* height) {
    @autoreleasepool {
        RTMPCameraCompatibility *compat = [RTMPCameraCompatibility sharedInstance];
        if (!compat.isRunning) return false;
        
        CGSize size = compat.settings.resolution;
        if (width) *width = (int)size.width;
        if (height) *height = (int)size.height;
        return true;
    }
}

float rtmp_camera_compat_get_framerate(void) {
    @autoreleasepool {
        return [RTMPCameraCompatibility sharedInstance].stats.currentFPS;
    }
}

uint64_t rtmp_camera_compat_get_frame_count(void) {
    @autoreleasepool {
        return [RTMPCameraCompatibility sharedInstance].stats.frameCount;
    }
}

bool rtmp_camera_compat_is_publishing(void) {
    @autoreleasepool {
        return [RTMPCameraCompatibility sharedInstance].stats.isPublishing;
    }
}