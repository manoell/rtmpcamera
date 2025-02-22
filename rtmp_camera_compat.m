#import <AVFoundation/AVFoundation.h>
#import <objc/runtime.h>
#import "rtmp_camera_compat.h"
#import "rtmp_stream.h"
#import "rtmp_diagnostics.h"

// Configurações da câmera
static const int kDefaultWidth = 1920;
static const int kDefaultHeight = 1080;
static const float kDefaultFrameRate = 30.0f;

@interface RTMPCameraCompatLayer () {
    CMFormatDescriptionRef _videoFormatDescription;
    dispatch_queue_t _cameraQueue;
    NSTimer *_frameTimer;
    uint64_t _frameNumber;
}

@property (nonatomic, strong) AVCaptureDevice *originalDevice;
@property (nonatomic, strong) AVCaptureSession *fakeSession;
@property (nonatomic, strong) AVCaptureVideoDataOutput *videoOutput;
@property (nonatomic, assign) rtmp_stream_t *stream;
@property (nonatomic, strong) NSMutableDictionary *deviceProperties;
@property (nonatomic, assign) BOOL isCapturing;
@property (nonatomic, assign) CMTime presentationTimeStamp;

@end

@implementation RTMPCameraCompatLayer

- (instancetype)init {
    self = [super init];
    if (self) {
        _cameraQueue = dispatch_queue_create("com.rtmpcamera.cameraqueue", 
            DISPATCH_QUEUE_SERIAL_WITH_AUTORELEASE_POOL);
        dispatch_set_target_queue(_cameraQueue, 
            dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
        
        _deviceProperties = [NSMutableDictionary dictionary];
        _frameNumber = 0;
        
        [self setupFakeSession];
        [self cloneOriginalCameraProperties];
        
        rtmp_diagnostics_log(LOG_INFO, "RTMPCameraCompatLayer inicializada");
    }
    return self;
}

- (void)setupFakeSession {
    self.fakeSession = [[AVCaptureSession alloc] init];
    self.fakeSession.sessionPreset = AVCaptureSessionPresetHigh;
    
    // Configuração otimizada do output
    self.videoOutput = [[AVCaptureVideoDataOutput alloc] init];
    [self.videoOutput setVideoSettings:@{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        (id)kCVPixelBufferMetalCompatibilityKey: @YES,
        (id)kCVPixelBufferWidthKey: @(kDefaultWidth),
        (id)kCVPixelBufferHeightKey: @(kDefaultHeight)
    }];
    
    self.videoOutput.alwaysDiscardsLateVideoFrames = YES;
    [self.videoOutput setSampleBufferDelegate:self queue:_cameraQueue];
    
    if ([self.fakeSession canAddOutput:self.videoOutput]) {
        [self.fakeSession addOutput:self.videoOutput];
    }
}

- (void)cloneOriginalCameraProperties {
    AVCaptureDevice *realCamera = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    self.originalDevice = realCamera;
    
    // Clona propriedades da câmera real
    self.deviceProperties[@"uniqueID"] = realCamera.uniqueID;
    self.deviceProperties[@"modelID"] = realCamera.modelID;
    self.deviceProperties[@"localizedName"] = realCamera.localizedName;
    self.deviceProperties[@"manufacturer"] = @"Apple";
    
    // Características da câmera
    self.deviceProperties[@"exposureDuration"] = @(CMTimeGetSeconds(realCamera.exposureDuration));
    self.deviceProperties[@"ISO"] = @(realCamera.ISO);
    self.deviceProperties[@"lensPosition"] = @(realCamera.lensPosition);
    self.deviceProperties[@"deviceType"] = @(realCamera.deviceType);
    
    // Capabilities
    self.deviceProperties[@"hasFlash"] = @(realCamera.hasFlash);
    self.deviceProperties[@"hasTorch"] = @(realCamera.hasTorch);
    self.deviceProperties[@"focusPointOfInterestSupported"] = @(realCamera.focusPointOfInterestSupported);
    self.deviceProperties[@"exposurePointOfInterestSupported"] = @(realCamera.exposurePointOfInterestSupported);
    
    // Formatos suportados
    NSMutableArray *formats = [NSMutableArray array];
    for (AVCaptureDeviceFormat *format in realCamera.formats) {
        CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(format.formatDescription);
        float maxFrameRate = ((AVFrameRateRange *)format.videoSupportedFrameRateRanges.firstObject).maxFrameRate;
        
        [formats addObject:@{
            @"width": @(dimensions.width),
            @"height": @(dimensions.height),
            @"frameRate": @(maxFrameRate)
        }];
    }
    self.deviceProperties[@"supportedFormats"] = formats;
}

- (void)startCapturing {
    if (self.isCapturing) return;
    
    dispatch_async(_cameraQueue, ^{
        self.isCapturing = YES;
        self.presentationTimeStamp = CMTimeMake(0, 1000);
        
        // Inicia timer para simular frames da câmera
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_frameTimer = [NSTimer scheduledTimerWithTimeInterval:1.0/kDefaultFrameRate
                                                              target:self
                                                            selector:@selector(generateFrame)
                                                            userInfo:nil
                                                             repeats:YES];
        });
        
        rtmp_diagnostics_log(LOG_INFO, "Captura iniciada");
    });
}

- (void)stopCapturing {
    if (!self.isCapturing) return;
    
    dispatch_async(_cameraQueue, ^{
        self.isCapturing = NO;
        
        dispatch_async(dispatch_get_main_queue(), ^{
            [self->_frameTimer invalidate];
            self->_frameTimer = nil;
        });
        
        rtmp_diagnostics_log(LOG_INFO, "Captura finalizada");
    });
}

- (void)generateFrame {
    if (!self.isCapturing) return;
    
    dispatch_async(_cameraQueue, ^{
        @autoreleasepool {
            CMTime timestamp = CMTimeAdd(self.presentationTimeStamp, 
                                       CMTimeMake(1000/kDefaultFrameRate, 1000));
            self.presentationTimeStamp = timestamp;
            
            // Obtém frame do stream RTMP
            video_frame_t *rtmpFrame = rtmp_stream_get_next_frame(self.stream);
            if (!rtmpFrame) return;
            
            // Cria buffer de pixel
            CVPixelBufferRef pixelBuffer = NULL;
            CVReturn status = CVPixelBufferCreate(kCFAllocatorDefault,
                                                kDefaultWidth,
                                                kDefaultHeight,
                                                kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
                                                NULL,
                                                &pixelBuffer);
            
            if (status != kCVReturnSuccess) {
                rtmp_diagnostics_log(LOG_ERROR, "Erro ao criar pixel buffer");
                return;
            }
            
            // Copia dados do frame RTMP para o buffer
            CVPixelBufferLockBaseAddress(pixelBuffer, 0);
            
            // Aqui você precisa converter os dados do frame RTMP para o formato YUV
            // Este é um exemplo simplificado
            uint8_t *baseAddress = CVPixelBufferGetBaseAddress(pixelBuffer);
            memcpy(baseAddress, rtmpFrame->data, rtmpFrame->length);
            
            CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
            
            // Cria sample buffer
            CMSampleBufferRef sampleBuffer = NULL;
            
            // Cria format description se necessário
            if (!_videoFormatDescription) {
                CMVideoFormatDescriptionCreateForImageBuffer(kCFAllocatorDefault,
                                                           pixelBuffer,
                                                           &_videoFormatDescription);
            }
            
            // Timing info
            CMSampleTimingInfo timing = {
                .duration = CMTimeMake(1, (int32_t)kDefaultFrameRate),
                .presentationTimeStamp = timestamp,
                .decodeTimeStamp = timestamp
            };
            
            // Cria sample buffer
            status = CMSampleBufferCreateForImageBuffer(kCFAllocatorDefault,
                                                      pixelBuffer,
                                                      TRUE,
                                                      NULL,
                                                      NULL,
                                                      _videoFormatDescription,
                                                      &timing,
                                                      &sampleBuffer);
            
            if (status == noErr && sampleBuffer) {
                // Notifica delegates
                if ([self.videoOutput.sampleBufferDelegate respondsToSelector:@selector(captureOutput:didOutputSampleBuffer:fromConnection:)]) {
                    [self.videoOutput.sampleBufferDelegate captureOutput:self.videoOutput
                                               didOutputSampleBuffer:sampleBuffer
                                                      fromConnection:self.videoOutput.connections.firstObject];
                }
                
                CFRelease(sampleBuffer);
            }
            
            CVPixelBufferRelease(pixelBuffer);
            
            _frameNumber++;
            
            // Log periódico de status
            if (_frameNumber % 300 == 0) { // A cada 10 segundos em 30fps
                rtmp_diagnostics_log(LOG_INFO, "Frames processados: %llu", _frameNumber);
            }
        }
    });
}

#pragma mark - Propriedades da Câmera

- (id)valueForKey:(NSString *)key {
    id value = self.deviceProperties[key];
    return value ?: [super valueForKey:key];
}

- (BOOL)supportsAVCaptureSessionPreset:(NSString *)preset {
    return [preset isEqualToString:AVCaptureSessionPresetHigh] ||
           [preset isEqualToString:AVCaptureSessionPreset1920x1080];
}

- (void)setExposureMode:(AVCaptureExposureMode)exposureMode {
    // Simula suporte a modos de exposição
}

- (void)setFocusMode:(AVCaptureFocusMode)focusMode {
    // Simula suporte a modos de foco
}

- (void)setWhiteBalanceMode:(AVCaptureWhiteBalanceMode)whiteBalanceMode {
    // Simula suporte a modos de white balance
}

#pragma mark - Method Forwarding

- (id)forwardingTargetForSelector:(SEL)aSelector {
    if ([self.originalDevice respondsToSelector:aSelector]) {
        return self.originalDevice;
    }
    return [super forwardingTargetForSelector:aSelector];
}

- (void)dealloc {
    [self stopCapturing];
    
    if (_videoFormatDescription) {
        CFRelease(_videoFormatDescription);
    }
    
    rtmp_diagnostics_log(LOG_INFO, "RTMPCameraCompatLayer finalizada");
}

@end