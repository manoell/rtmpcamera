#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import "rtmp_core.h"
#import "rtmp_preview.h"

@interface RTMPCameraManager : NSObject

@property (nonatomic, strong) dispatch_queue_t sessionQueue;
@property (nonatomic, strong) AVCaptureSession *captureSession;
@property (nonatomic, strong) AVCaptureVideoDataOutput *videoOutput;
@property (nonatomic, strong) AVCaptureConnection *videoConnection;
@property (nonatomic, assign) BOOL isConfigured;
@property (nonatomic, assign) BOOL isStreaming;
@property (nonatomic, strong) RTMPPreviewManager *previewManager;

+ (instancetype)sharedInstance;
- (void)startServer;
- (void)stopServer;
- (void)setupVideoCapture;
- (void)startVideoCapture;
- (void)stopVideoCapture;
- (void)injectRTMPStream;

@end

@implementation RTMPCameraManager

+ (instancetype)sharedInstance {
    static RTMPCameraManager *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[self alloc] init];
    });
    return instance;
}

- (instancetype)init {
    if (self = [super init]) {
        _sessionQueue = dispatch_queue_create("com.rtmpcamera.session", DISPATCH_QUEUE_SERIAL);
        _previewManager = [RTMPPreviewManager sharedInstance];
        _isConfigured = NO;
        _isStreaming = NO;
    }
    return self;
}

- (void)startServer {
    // Iniciar servidor RTMP na porta padrão 1935
    int result = rtmp_server_start(1935, 
        ^(uint8_t *data, size_t length, uint32_t timestamp) {
            // Callback de vídeo
            [self.previewManager processVideoFrame:data length:length timestamp:timestamp];
        },
        ^(uint8_t *data, size_t length, uint32_t timestamp) {
            // Callback de áudio
            [self.previewManager processAudioFrame:data length:length timestamp:timestamp];
        }
    );
    
    if (result == 0) {
        NSLog(@"RTMP Server started successfully");
        [self setupVideoCapture];
    } else {
        NSLog(@"Failed to start RTMP Server");
    }
}

- (void)stopServer {
    rtmp_server_stop();
    [self stopVideoCapture];
}

- (void)setupVideoCapture {
    if (self.isConfigured) return;
    
    dispatch_async(self.sessionQueue, ^{
        self.captureSession = [[AVCaptureSession alloc] init];
        
        // Configurar qualidade alta
        if ([self.captureSession canSetSessionPreset:AVCaptureSessionPreset1920x1080]) {
            self.captureSession.sessionPreset = AVCaptureSessionPreset1920x1080;
        }
        
        // Configurar saída de vídeo
        self.videoOutput = [[AVCaptureVideoDataOutput alloc] init];
        
        // Configurar formato de pixel
        NSDictionary *settings = @{
            (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
            (id)kCVPixelBufferWidthKey: @1920,
            (id)kCVPixelBufferHeightKey: @1080
        };
        self.videoOutput.videoSettings = settings;
        
        // Configurar queue para processamento
        dispatch_queue_t videoQueue = dispatch_queue_create("com.rtmpcamera.video", DISPATCH_QUEUE_SERIAL);
        [self.videoOutput setSampleBufferDelegate:self queue:videoQueue];
        
        // Descartar frames atrasados para manter sincronização
        self.videoOutput.alwaysDiscardsLateVideoFrames = YES;
        
        if ([self.captureSession canAddOutput:self.videoOutput]) {
            [self.captureSession addOutput:self.videoOutput];
        }
        
        self.videoConnection = [self.videoOutput connectionWithMediaType:AVMediaTypeVideo];
        
        // Configurar orientação
        if (self.videoConnection.isVideoOrientationSupported) {
            self.videoConnection.videoOrientation = AVCaptureVideoOrientationPortrait;
        }
        
        // Configurar estabilização
        if (self.videoConnection.isVideoStabilizationSupported) {
            self.videoConnection.preferredVideoStabilizationMode = AVCaptureVideoStabilizationModeStandard;
        }
        
        self.isConfigured = YES;
    });
}

- (void)startVideoCapture {
    if (!self.isConfigured || self.isStreaming) return;
    
    dispatch_async(self.sessionQueue, ^{
        [self.captureSession startRunning];
        self.isStreaming = YES;
        [self.previewManager startProcessing];
    });
}

- (void)stopVideoCapture {
    if (!self.isStreaming) return;
    
    dispatch_async(self.sessionQueue, ^{
        [self.captureSession stopRunning];
        self.isStreaming = NO;
        [self.previewManager stopProcessing];
    });
}

- (void)injectRTMPStream {
    // Substituir feed da câmera do sistema pelo stream RTMP
    Method originalMethod = class_getInstanceMethod([AVCaptureSession class], 
                                                  @selector(startRunning));
    Method customMethod = class_getInstanceMethod([self class], 
                                                @selector(customStartRunning));
    method_exchangeImplementations(originalMethod, customMethod);
}

// Método para substituir o feed da câmera
- (void)customStartRunning {
    if (!self.isStreaming) {
        [self startVideoCapture];
    }
    
    // Configurar características da câmera para parecer real
    CMTime frameDuration = CMTimeMake(1, 30); // 30 fps
    NSDictionary *deviceCharacteristics = @{
        @"uniqueID": @"com.rtmpcamera.virtual",
        @"modelID": @"iPhone Camera",
        @"localizedName": @"Back Camera",
        @"frameDuration": [NSValue valueWithCMTime:frameDuration],
        @"hasFlash": @YES,
        @"hasTorch": @YES,
        @"hasAutoFocus": @YES,
        @"maxISO": @2000,
        @"minISO": @50
    };
    
    objc_setAssociatedObject(self, "DeviceCharacteristics", 
                            deviceCharacteristics, 
                            OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

#pragma mark - AVCaptureVideoDataOutputSampleBufferDelegate

- (void)captureOutput:(AVCaptureOutput *)output 
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer 
       fromConnection:(AVCaptureConnection *)connection {
    if (!self.isStreaming) return;
    
    // Processar frame recebido da câmera virtual
    CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
    if (imageBuffer == NULL) return;
    
    CVPixelBufferLockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
    
    size_t width = CVPixelBufferGetWidth(imageBuffer);
    size_t height = CVPixelBufferGetHeight(imageBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(imageBuffer);
    uint8_t *baseAddress = (uint8_t *)CVPixelBufferGetBaseAddress(imageBuffer);
    
    // Criar buffer para codificação H.264
    NSMutableData *encodedData = [NSMutableData data];
    
    // Codificar frame para H.264
    VTCompressionSessionRef encodingSession = NULL;
    VTCompressionSessionCreate(NULL, width, height, kCMVideoCodecType_H264, 
                             NULL, NULL, NULL, NULL, NULL, &encodingSession);
    
    if (encodingSession) {
        // Configurar parâmetros de codificação
        VTSessionSetProperty(encodingSession, kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
        VTSessionSetProperty(encodingSession, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_High_AutoLevel);
        VTSessionSetProperty(encodingSession, kVTCompressionPropertyKey_AverageBitRate, (__bridge CFTypeRef)@(2000000));
        VTSessionSetProperty(encodingSession, kVTCompressionPropertyKey_MaxKeyFrameInterval, (__bridge CFTypeRef)@(30));
        
        // Codificar frame
        CMTime presentationTime = CMSampleBufferGetPresentationTimeStamp(sampleBuffer);
        VTCompressionSessionEncodeFrame(encodingSession, imageBuffer, presentationTime, 
                                      kCMTimeInvalid, NULL, NULL, NULL);
        
        VTCompressionSessionCompleteFrames(encodingSession, kCMTimeInvalid);
        VTCompressionSessionInvalidate(encodingSession);
        CFRelease(encodingSession);
    }
    
    CVPixelBufferUnlockBaseAddress(imageBuffer, kCVPixelBufferLock_ReadOnly);
    
    // Enviar frame codificado para o preview
    [self.previewManager processVideoFrame:encodedData.bytes 
                                  length:encodedData.length 
                               timestamp:CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sampleBuffer)) * 1000];
}

@end