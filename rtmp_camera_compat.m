#import "rtmp_camera_compat.h"
#import "rtmp_utils.h"
#import <VideoToolbox/VideoToolbox.h>

@interface RTMPCameraCompatibility () {
    RTMPCameraConfig _config;
    RTMPCameraStatus _status;
    RTMPStream *_stream;
    dispatch_queue_t _captureQueue;
    dispatch_queue_t _encodeQueue;
}

@property (nonatomic, strong) AVCaptureSession *captureSession;
@property (nonatomic, strong) AVCaptureDevice *videoDevice;
@property (nonatomic, strong) AVCaptureDevice *audioDevice;
@property (nonatomic, strong) AVCaptureDeviceInput *videoInput;
@property (nonatomic, strong) AVCaptureDeviceInput *audioInput;
@property (nonatomic, strong) AVCaptureVideoDataOutput *videoOutput;
@property (nonatomic, strong) AVCaptureAudioDataOutput *audioOutput;
@property (nonatomic, strong) AVCaptureVideoPreviewLayer *previewLayer;
@property (nonatomic, strong) VTCompressionSessionRef compressionSession;
@property (nonatomic, strong) CMFormatDescriptionRef formatDescription;
@property (nonatomic, assign) CMTime presentationTime;

@property (nonatomic, strong) NSArray<AVCaptureDevice *> *availableDevices;
@property (nonatomic, weak) UIView *previewView;

@property (nonatomic, copy) RTMPCameraStateCallback stateCallback;
@property (nonatomic, copy) RTMPCameraErrorCallback errorCallback;
@property (nonatomic, copy) RTMPCameraFaceDetectionCallback faceCallback;

@end

@implementation RTMPCameraCompatibility

#pragma mark - Initialization

+ (instancetype)sharedInstance {
    static RTMPCameraCompatibility *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[self alloc] init];
    });
    return instance;
}

- (instancetype)init {
    if (self = [super init]) {
        _captureQueue = dispatch_queue_create("com.rtmpcamera.capture", DISPATCH_QUEUE_SERIAL);
        _encodeQueue = dispatch_queue_create("com.rtmpcamera.encode", DISPATCH_QUEUE_SERIAL);
        
        // Set default config
        _config.width = 1280;
        _config.height = 720;
        _config.frameRate = 30;
        _config.bitrate = 1000000;
        _config.keyframeInterval = 2;
        _config.jpegQuality = 0.8;
        _config.enableHardwareEncoder = YES;
        _config.enableFaceDetection = NO;
        _config.enableStabilization = YES;
        _config.maintainAspectRatio = YES;
        _config.orientation = RTMP_CAMERA_ORIENTATION_PORTRAIT;
        _config.position = AVCaptureDevicePositionBack;
        
        _status.state = RTMP_CAMERA_STATE_IDLE;
        
        // Setup capture session
        _captureSession = [[AVCaptureSession alloc] init];
        
        // Load available devices
        [self loadAvailableDevices];
        
        // Register for orientation changes
        [[NSNotificationCenter defaultCenter] addObserver:self
                                               selector:@selector(orientationChanged:)
                                                   name:UIDeviceOrientationDidChangeNotification
                                                 object:nil];
        
        // Register for app lifecycle events
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
    [self stopCapture];
    [[NSNotificationCenter defaultCenter] removeObserver:self];
}

#pragma mark - Camera Control

- (BOOL)startCapture {
    if (_status.state != RTMP_CAMERA_STATE_IDLE) {
        return NO;
    }
    
    _status.state = RTMP_CAMERA_STATE_STARTING;
    
    // Configure capture session
    if (![self configureCaptureSession]) {
        _status.state = RTMP_CAMERA_STATE_ERROR;
        return NO;
    }
    
    // Configure hardware encoder if enabled
    if (_config.enableHardwareEncoder) {
        if (![self configureHardwareEncoder]) {
            _config.enableHardwareEncoder = NO;
        }
    }
    
    // Start capture session
    dispatch_async(_captureQueue, ^{
        [self.captureSession startRunning];
        
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_status.state = RTMP_CAMERA_STATE_CAPTURING;
            if (self.stateCallback) {
                self.stateCallback(self->_status.state);
            }
        });
    });
    
    return YES;
}

- (void)stopCapture {
    if (_status.state == RTMP_CAMERA_STATE_IDLE) {
        return;
    }
    
    dispatch_async(_captureQueue, ^{
        [self.captureSession stopRunning];
        
        // Cleanup hardware encoder
        if (self->_compressionSession) {
            VTCompressionSessionCompleteFrames(self->_compressionSession, kCMTimeInvalid);
            VTCompressionSessionInvalidate(self->_compressionSession);
            CFRelease(self->_compressionSession);
            self->_compressionSession = NULL;
        }
        
        // Cleanup format description
        if (self->_formatDescription) {
            CFRelease(self->_formatDescription);
            self->_formatDescription = NULL;
        }
        
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_status.state = RTMP_CAMERA_STATE_IDLE;
            if (self.stateCallback) {
                self.stateCallback(self->_status.state);
            }
        });
    });
}

- (void)pauseCapture {
    if (_status.state != RTMP_CAMERA_STATE_CAPTURING) {
        return;
    }
    
    dispatch_async(_captureQueue, ^{
        [self.captureSession stopRunning];
        
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_status.state = RTMP_CAMERA_STATE_PAUSED;
            if (self.stateCallback) {
                self.stateCallback(self->_status.state);
            }
        });
    });
}

- (void)resumeCapture {
    if (_status.state != RTMP_CAMERA_STATE_PAUSED) {
        return;
    }
    
    dispatch_async(_captureQueue, ^{
        [self.captureSession startRunning];
        
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_status.state = RTMP_CAMERA_STATE_CAPTURING;
            if (self.stateCallback) {
                self.stateCallback(self->_status.state);
            }
        });
    });
}

#pragma mark - Configuration

- (BOOL)configureCaptureSession {
    [_captureSession beginConfiguration];
    
    // Set session preset
    if ([_captureSession canSetSessionPreset:AVCaptureSessionPreset1280x720]) {
        _captureSession.sessionPreset = AVCaptureSessionPreset1280x720;
    } else {
        [_captureSession commitConfiguration];
        [self notifyError:@"Failed to set capture session preset"];
        return NO;
    }
    
    // Configure video input
    NSError *error = nil;
    _videoDevice = [self findDeviceWithPosition:_config.position];
    if (!_videoDevice) {
        [_captureSession commitConfiguration];
        [self notifyError:@"Failed to find video device"];
        return NO;
    }
    
    _videoInput = [AVCaptureDeviceInput deviceInputWithDevice:_videoDevice error:&error];
    if (!_videoInput || error) {
        [_captureSession commitConfiguration];
        [self notifyError:error.localizedDescription];
        return NO;
    }
    
    if ([_captureSession canAddInput:_videoInput]) {
        [_captureSession addInput:_videoInput];
    } else {
        [_captureSession commitConfiguration];
        [self notifyError:@"Failed to add video input"];
        return NO;
    }
    
    // Configure audio input
    _audioDevice = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio];
    _audioInput = [AVCaptureDeviceInput deviceInputWithDevice:_audioDevice error:&error];
    if (!_audioInput || error) {
        [_captureSession commitConfiguration];
        [self notifyError:error.localizedDescription];
        return NO;
    }
    
    if ([_captureSession canAddInput:_audioInput]) {
        [_captureSession addInput:_audioInput];
    } else {
        [_captureSession commitConfiguration];
        [self notifyError:@"Failed to add audio input"];
        return NO;
    }
    
    // Configure video output
    _videoOutput = [[AVCaptureVideoDataOutput alloc] init];
    _videoOutput.videoSettings = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        (id)kCVPixelBufferWidthKey: @(_config.width),
        (id)kCVPixelBufferHeightKey: @(_config.height)
    };
    _videoOutput.alwaysDiscardsLateVideoFrames = YES;
    [_videoOutput setSampleBufferDelegate:self queue:_captureQueue];
    
    if ([_captureSession canAddOutput:_videoOutput]) {
        [_captureSession addOutput:_videoOutput];
    } else {
        [_captureSession commitConfiguration];
        [self notifyError:@"Failed to add video output"];
        return NO;
    }
    
    // Configure audio output
    _audioOutput = [[AVCaptureAudioDataOutput alloc] init];
    [_audioOutput setSampleBufferDelegate:self queue:_captureQueue];
    
    if ([_captureSession canAddOutput:_audioOutput]) {
        [_captureSession addOutput:_audioOutput];
    } else {
        [_captureSession commitConfiguration];
        [self notifyError:@"Failed to add audio output"];
        return NO;
    }
    
    // Configure video connection
    AVCaptureConnection *videoConnection = [_videoOutput connectionWithMediaType:AVMediaTypeVideo];
    if ([videoConnection isVideoStabilizationSupported] && _config.enableStabilization) {
        videoConnection.preferredVideoStabilizationMode = AVCaptureVideoStabilizationModeAuto;
    }
    
    videoConnection.videoOrientation = [self avOrientationFromRTMPOrientation:_config.orientation];
    
    // Configure preview layer
    if (_previewView) {
        _previewLayer = [AVCaptureVideoPreviewLayer layerWithSession:_captureSession];
        _previewLayer.videoGravity = AVLayerVideoGravityResizeAspectFill;
        _previewLayer.frame = _previewView.bounds;
        [_previewView.layer addSublayer:_previewLayer];
    }
    
    [_captureSession commitConfiguration];
    return YES;
}

- (BOOL)configureHardwareEncoder {
    // Create compression session
    OSStatus status = VTCompressionSessionCreate(NULL,
                                               _config.width,
                                               _config.height,
                                               kCMVideoCodecType_H264,
                                               NULL,
                                               NULL,
                                               NULL,
                                               VideoCompressedCallback,
                                               (__bridge void *)self,
                                               &_compressionSession);
    
    if (status != noErr) {
        [self notifyError:@"Failed to create compression session"];
        return NO;
    }
    
    // Configure encoder properties
    status = VTSessionSetProperty(_compressionSession,
                                kVTCompressionPropertyKey_RealTime,
                                kCFBooleanTrue);
    
    status |= VTSessionSetProperty(_compressionSession,
                                 kVTCompressionPropertyKey_ProfileLevel,
                                 kVTProfileLevel_H264_Baseline_AutoLevel);
    
    status |= VTSessionSetProperty(_compressionSession,
                                 kVTCompressionPropertyKey_AllowFrameReordering,
                                 kCFBooleanFalse);
    
    status |= VTSessionSetProperty(_compressionSession,
                                 kVTCompressionPropertyKey_MaxKeyFrameInterval,
                                 (__bridge CFTypeRef)@(_config.keyframeInterval * _config.frameRate));
    
    status |= VTSessionSetProperty(_compressionSession,
                                 kVTCompressionPropertyKey_ExpectedFrameRate,
                                 (__bridge CFTypeRef)@(_config.frameRate));
    
    status |= VTSessionSetProperty(_compressionSession,
                                 kVTCompressionPropertyKey_AverageBitRate,
                                 (__bridge CFTypeRef)@(_config.bitrate));
    
    if (status != noErr) {
        [self notifyError:@"Failed to configure compression session"];
        return NO;
    }
    
    status = VTCompressionSessionPrepareToEncodeFrames(_compressionSession);
    if (status != noErr) {
        [self notifyError:@"Failed to prepare compression session"];
        return NO;
    }
    
    return YES;
}

#pragma mark - AVCaptureVideoDataOutputSampleBufferDelegate

- (void)captureOutput:(AVCaptureOutput *)output 
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer 
       fromConnection:(AVCaptureConnection *)connection {
    
    if (!_stream || _status.state != RTMP_CAMERA_STATE_CAPTURING) {
        return;
    }
    
    CFRetain(sampleBuffer);
    
    if (connection == [_videoOutput connectionWithMediaType:AVMediaTypeVideo]) {
        // Handle video sample buffer
        dispatch_async(_encodeQueue, ^{
            [self processVideoSampleBuffer:sampleBuffer];
            CFRelease(sampleBuffer);
        });
    } else if (connection == [_audioOutput connectionWithMediaType:AVMediaTypeAudio]) {
        // Handle audio sample buffer
        dispatch_async(_encodeQueue, ^{
            [self processAudioSampleBuffer:sampleBuffer];
            CFRelease(sampleBuffer);
        });
    }
}

#pragma mark - Sample Buffer Processing

- (void)processVideoSampleBuffer:(CMSampleBufferRef)sampleBuffer {
    _status.framesCapture++;
    
    uint32_t captureTime = rtmp_get_timestamp();

    if (_config.enableHardwareEncoder) {
        // Hardware encoding
        CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
        if (imageBuffer == NULL) {
            return;
        }

        // Get frame timing info
        CMTime presentationTimeStamp = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
        CMTime duration = CMSampleBufferGetDuration(sampleBuffer);
        
        // Prepare encode info
        VTEncodeInfoFlags flags;
        OSStatus status = VTCompressionSessionEncodeFrame(_compressionSession,
                                                        imageBuffer,
                                                        presentationTimeStamp,
                                                        duration,
                                                        NULL,
                                                        NULL,
                                                        &flags);
        
        if (status != noErr) {
            [self notifyError:@"Failed to encode video frame"];
            return;
        }
    } else {
        // Software encoding
        CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
        if (imageBuffer == NULL) {
            return;
        }

        // Lock base address
        CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
        
        // Get buffer dimensions
        size_t width = CVPixelBufferGetWidth(imageBuffer);
        size_t height = CVPixelBufferGetHeight(imageBuffer);
        size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
        uint8_t *baseAddress = CVPixelBufferGetBaseAddress(imageBuffer);
        
        // Create RTMP packet
        RTMPPacket packet;
        memset(&packet, 0, sizeof(packet));
        packet.type = RTMP_MSG_VIDEO;
        packet.timestamp = rtmp_get_timestamp();
        
        // Convert to YUV and encode
        // TODO: Implement software encoding
        
        // Send packet
        if (_stream) {
            rtmp_send_packet(_stream->rtmp, &packet);
        }
        
        // Unlock buffer
        CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
        
        // Update stats
        _status.framesEncoded++;
        _status.framesSent++;
        _status.encodeTime = rtmp_get_timestamp() - captureTime;
    }
}

- (void)processAudioSampleBuffer:(CMSampleBufferRef)sampleBuffer {
    if (!_stream) return;

    // Get audio data
    CMBlockBufferRef blockBuffer;
    AudioBufferList audioBufferList;
    CMSampleBufferGetAudioBufferList(sampleBuffer, NULL, &audioBufferList, sizeof(audioBufferList));
    CMSampleBufferGetDataBuffer(sampleBuffer, &blockBuffer);
    
    // Create RTMP packet
    RTMPPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = RTMP_MSG_AUDIO;
    packet.timestamp = rtmp_get_timestamp();
    
    for (int i = 0; i < audioBufferList.mNumberBuffers; i++) {
        AudioBuffer audioBuffer = audioBufferList.mBuffers[i];
        
        // Encode audio data
        // TODO: Implement AAC encoding
        
        // Send packet
        if (_stream) {
            rtmp_send_packet(_stream->rtmp, &packet);
        }
    }
}

#pragma mark - Hardware Encoder Callback

static void VideoCompressedCallback(void *outputCallbackRefCon,
                                  void *sourceFrameRefCon,
                                  OSStatus status,
                                  VTEncodeInfoFlags infoFlags,
                                  CMSampleBufferRef sampleBuffer) {
    if (status != noErr || !sampleBuffer) return;
    
    RTMPCameraCompatibility *camera = (__bridge RTMPCameraCompatibility *)outputCallbackRefCon;
    [camera handleEncodedVideoFrame:sampleBuffer];
}

- (void)handleEncodedVideoFrame:(CMSampleBufferRef)sampleBuffer {
    // Get encoded data
    CMBlockBufferRef blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer);
    size_t length, totalLength;
    char *dataPointer;
    OSStatus status = CMBlockBufferGetDataPointer(blockBuffer, 0, &length, &totalLength, &dataPointer);
    
    if (status != kCMBlockBufferNoErr) return;
    
    // Create RTMP packet
    RTMPPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = RTMP_MSG_VIDEO;
    packet.timestamp = rtmp_get_timestamp();
    
    // Check for keyframe
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, YES);
    BOOL keyframe = NO;
    
    if (attachments != NULL && CFArrayGetCount(attachments) > 0) {
        CFDictionaryRef attachment = (CFDictionaryRef)CFArrayGetValueAtIndex(attachments, 0);
        keyframe = !CFDictionaryContainsKey(attachment, kCMSampleAttachmentKey_NotSync);
    }
    
    // Set packet data
    uint8_t *data = malloc(totalLength + 5);
    data[0] = keyframe ? 0x17 : 0x27;
    data[1] = 0x01; // AVC NALU
    data[2] = 0x00;
    data[3] = 0x00;
    data[4] = 0x00;
    memcpy(data + 5, dataPointer, totalLength);
    
    packet.data = data;
    packet.size = totalLength + 5;
    
    // Send packet
    if (_stream) {
        rtmp_send_packet(_stream->rtmp, &packet);
    }
    
    free(data);
    
    // Update stats
    _status.framesEncoded++;
    _status.framesSent++;
    _status.currentBitrate = totalLength * 8 * _config.frameRate; // approximate
}

#pragma mark - Device Management

- (void)loadAvailableDevices {
    NSMutableArray *devices = [NSMutableArray array];
    
    AVCaptureDeviceDiscoverySession *discoverySession = [AVCaptureDeviceDiscoverySession 
        discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera]
        mediaType:AVMediaTypeVideo
        position:AVCaptureDevicePositionUnspecified];
    
    for (AVCaptureDevice *device in discoverySession.devices) {
        [devices addObject:device];
    }
    
    _availableDevices = [devices copy];
}

- (AVCaptureDevice *)findDeviceWithPosition:(AVCaptureDevicePosition)position {
    for (AVCaptureDevice *device in _availableDevices) {
        if (device.position == position) {
            return device;
        }
    }
    return nil;
}

#pragma mark - Utility Methods

- (void)notifyError:(NSString *)message {
    NSError *error = [NSError errorWithDomain:@"RTMPCamera"
                                        code:-1
                                    userInfo:@{NSLocalizedDescriptionKey: message}];
    
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self.errorCallback) {
            self.errorCallback(error);
        }
    });
}

- (AVCaptureVideoOrientation)avOrientationFromRTMPOrientation:(RTMPCameraOrientation)orientation {
    switch (orientation) {
        case RTMP_CAMERA_ORIENTATION_PORTRAIT:
            return AVCaptureVideoOrientationPortrait;
        case RTMP_CAMERA_ORIENTATION_LANDSCAPE_LEFT:
            return AVCaptureVideoOrientationLandscapeLeft;
        case RTMP_CAMERA_ORIENTATION_LANDSCAPE_RIGHT:
            return AVCaptureVideoOrientationLandscapeRight;
        case RTMP_CAMERA_ORIENTATION_PORTRAIT_UPSIDE_DOWN:
            return AVCaptureVideoOrientationPortraitUpsideDown;
        default:
            return AVCaptureVideoOrientationPortrait;
    }
}

#pragma mark - Notifications

- (void)orientationChanged:(NSNotification *)notification {
    UIDeviceOrientation deviceOrientation = [[UIDevice currentDevice] orientation];
    [self handleDeviceOrientationChange:deviceOrientation];
}

- (void)applicationWillResignActive:(NSNotification *)notification {
    [self pauseCapture];
}

- (void)applicationDidBecomeActive:(NSNotification *)notification {
    if (_status.state == RTMP_CAMERA_STATE_PAUSED) {
        [self resumeCapture];
    }
}

@end