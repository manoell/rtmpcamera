#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import "rtmp_camera.h"

// Constantes para simulação de dispositivo
#define CAMERA_UNIQUE_ID @"com.rtmpcamera.virtual"
#define CAMERA_MODEL_ID @"iPhone Pro Camera"
#define CAMERA_MANUFACTURER @"Apple"
#define CAMERA_SERIAL @"RTMPCAM001"

@interface RTMPCameraCompatibility : NSObject

@property (nonatomic, strong) NSMutableDictionary *appProfiles;
@property (nonatomic, strong) NSMutableDictionary *activeCaptureSessions;
@property (nonatomic, strong) dispatch_queue_t compatQueue;
@property (nonatomic, assign) BOOL isOverriding;

+ (instancetype)sharedInstance;
- (void)startCameraOverride;
- (void)stopCameraOverride;
- (void)setupForApp:(NSString *)bundleId;

@end

@implementation RTMPCameraCompatibility {
    CMVideoDimensions _defaultDimensions;
    AVCaptureDeviceFormat *_virtualFormat;
    NSArray *_supportedFormats;
    Method _originalDeviceMethod;
    Method _originalSessionMethod;
}

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
        _appProfiles = [NSMutableDictionary dictionary];
        _activeCaptureSessions = [NSMutableDictionary dictionary];
        _compatQueue = dispatch_queue_create("com.rtmpcamera.compat", DISPATCH_QUEUE_SERIAL);
        _defaultDimensions = (CMVideoDimensions){1920, 1080};
        
        [self setupDefaultProfiles];
        [self setupVirtualFormats];
    }
    return self;
}

- (void)setupDefaultProfiles {
    // Perfis otimizados para apps populares
    _appProfiles = @{
        @"com.apple.camera": @{
            @"name": @"Camera",
            @"formats": @[@"1920x1080", @"1280x720", @"720x480"],
            @"features": @{
                @"flash": @YES,
                @"torch": @YES,
                @"focusPoint": @YES,
                @"exposurePoint": @YES,
                @"whiteBalance": @YES
            },
            @"metadata": @{
                @"isSystemCamera": @YES,
                @"position": @"back",
                @"uniqueID": CAMERA_UNIQUE_ID
            }
        },
        @"com.google.ios.youtube": @{
            @"name": @"YouTube",
            @"formats": @[@"1920x1080", @"1280x720"],
            @"features": @{
                @"flash": @YES,
                @"torch": @YES,
                @"focusPoint": @YES
            }
        },
        @"com.facebook.Facebook": @{
            @"name": @"Facebook",
            @"formats": @[@"1280x720", @"854x480"],
            @"features": @{
                @"flash": @YES,
                @"focusPoint": @YES
            }
        },
        // Perfil genérico para outros apps
        @"default": @{
            @"name": @"Generic",
            @"formats": @[@"1920x1080", @"1280x720"],
            @"features": @{
                @"flash": @YES,
                @"torch": @YES,
                @"focusPoint": @YES
            }
        }
    };
}

- (void)setupVirtualFormats {
    NSMutableArray *formats = [NSMutableArray array];
    
    // Criar formatos suportados
    NSArray *dimensions = @[
        @[@1920, @1080],
        @[@1280, @720],
        @[@854, @480],
        @[@640, @480]
    ];
    
    for (NSArray *dim in dimensions) {
        CMVideoFormatDescriptionRef format;
        
        FourCharCode pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        
        CMVideoFormatDescriptionCreate(kCFAllocatorDefault,
                                     kCMVideoCodecType_H264,
                                     [dim[0] intValue],
                                     [dim[1] intValue],
                                     (__bridge CFDictionaryRef)@{
            (id)kCVPixelBufferPixelFormatTypeKey: @(pixelFormat)
        },
                                     &format);
        
        AVFrameRateRange *frameRate = [[AVFrameRateRange alloc] init];
        [frameRate setValue:@(30) forKey:@"_maxFrameRate"];
        [frameRate setValue:@(30) forKey:@"_minFrameRate"];
        
        AVCaptureDeviceFormat *deviceFormat = [[AVCaptureDeviceFormat alloc] init];
        [deviceFormat setValue:(__bridge id)format forKey:@"_formatDescription"];
        [deviceFormat setValue:@[frameRate] forKey:@"_frameRateRanges"];
        
        [formats addObject:deviceFormat];
        CFRelease(format);
    }
    
    _supportedFormats = formats;
    _virtualFormat = formats[0];
}

- (void)startCameraOverride {
    if (_isOverriding) return;
    
    // Substituir métodos do AVCaptureDevice
    Class deviceClass = [AVCaptureDevice class];
    
    _originalDeviceMethod = class_getClassMethod(deviceClass, @selector(devicesWithMediaType:));
    Method customMethod = class_getClassMethod([self class], @selector(customDevicesWithMediaType:));
    method_exchangeImplementations(_originalDeviceMethod, customMethod);
    
    // Substituir métodos do AVCaptureSession
    Class sessionClass = [AVCaptureSession class];
    
    _originalSessionMethod = class_getInstanceMethod(sessionClass, @selector(startRunning));
    Method customSessionMethod = class_getInstanceMethod([self class], @selector(customStartRunning));
    method_exchangeImplementations(_originalSessionMethod, customSessionMethod);
    
    _isOverriding = YES;
}

- (void)stopCameraOverride {
    if (!_isOverriding) return;
    
    // Restaurar métodos originais
    method_exchangeImplementations(_originalDeviceMethod, 
                                 class_getClassMethod([self class], @selector(customDevicesWithMediaType:)));
    
    method_exchangeImplementations(_originalSessionMethod,
                                 class_getInstanceMethod([self class], @selector(customStartRunning)));
    
    _isOverriding = NO;
}

+ (NSArray<AVCaptureDevice *> *)customDevicesWithMediaType:(NSString *)mediaType {
    // Primeiro obtém dispositivos reais
    NSArray *originalDevices = ((NSArray<AVCaptureDevice *> *(*)(id, SEL, NSString *))
                               method_getImplementation([[self class] instanceMethodForSelector:@selector(originalDevicesWithMediaType:)]))
    ([self class], @selector(originalDevicesWithMediaType:), mediaType);
    
    if (![mediaType isEqualToString:AVMediaTypeVideo]) {
        return originalDevices;
    }
    
    // Criar dispositivo virtual
    AVCaptureDevice *virtualDevice = [[AVCaptureDevice alloc] init];
    
    // Configurar características
    [virtualDevice setValue:CAMERA_UNIQUE_ID forKey:@"_uniqueID"];
    [virtualDevice setValue:CAMERA_MODEL_ID forKey:@"_modelID"];
    [virtualDevice setValue:@"Back Camera" forKey:@"_localizedName"];
    [virtualDevice setValue:@(AVCaptureDevicePositionBack) forKey:@"_position"];
    
    // Configurar capacidades
    [virtualDevice setValue:@YES forKey:@"_hasFlash"];
    [virtualDevice setValue:@YES forKey:@"_hasTorch"];
    [virtualDevice setValue:@YES forKey:@"_hasAutoFocus"];
    [virtualDevice setValue:@YES forKey:@"_hasAutoExposure"];
    
    // Configurar formatos
    RTMPCameraCompatibility *compat = [RTMPCameraCompatibility sharedInstance];
    [virtualDevice setValue:compat->_supportedFormats forKey:@"_formats"];
    [virtualDevice setValue:compat->_virtualFormat forKey:@"_activeFormat"];
    
    // Adicionar à lista
    NSMutableArray *devices = [originalDevices mutableCopy];
    [devices addObject:virtualDevice];
    
    return devices;
}

- (void)customStartRunning {
    // Verificar se é nossa sessão virtual
    AVCaptureSession *session = (AVCaptureSession *)self;
    
    if ([self isVirtualSession:session]) {
        // Iniciar preview RTMP
        [[RTMPPreviewManager sharedInstance] startProcessing];
    } else {
        // Chamar implementação original
        ((void(*)(id, SEL))
         method_getImplementation([[self class] instanceMethodForSelector:@selector(originalStartRunning)]))
        (self, @selector(originalStartRunning));
    }
}

- (BOOL)isVirtualSession:(AVCaptureSession *)session {
    for (AVCaptureInput *input in session.inputs) {
        if ([input isKindOfClass:[AVCaptureDeviceInput class]]) {
            AVCaptureDeviceInput *deviceInput = (AVCaptureDeviceInput *)input;
            if ([deviceInput.device.uniqueID isEqualToString:CAMERA_UNIQUE_ID]) {
                return YES;
            }
        }
    }
    return NO;
}

- (void)setupForApp:(NSString *)bundleId {
    dispatch_async(_compatQueue, ^{
        // Obter perfil do app
        NSDictionary *profile = self.appProfiles[bundleId];
        if (!profile) {
            profile = self.appProfiles[@"default"];
        }
        
        // Ajustar formatos baseado no perfil
        NSArray *formats = profile[@"formats"];
        if (formats.count > 0) {
            NSString *bestFormat = formats[0];
            NSArray *dimensions = [bestFormat componentsSeparatedByString:@"x"];
            if (dimensions.count == 2) {
                _defaultDimensions.width = [dimensions[0] intValue];
                _defaultDimensions.height = [dimensions[1] intValue];
                
                // Atualizar formato ativo
                for (AVCaptureDeviceFormat *format in _supportedFormats) {
                    CMVideoFormatDescriptionRef desc = (__bridge CMVideoFormatDescriptionRef)[format valueForKey:@"_formatDescription"];
                    if (CMVideoFormatDescriptionGetDimensions(desc).width == _defaultDimensions.width) {
                        _virtualFormat = format;
                        break;
                    }
                }
            }
        }
        
        // Registrar sessão ativa
        self.activeCaptureSessions[bundleId] = @YES;
    });
}

@end