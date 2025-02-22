#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import "rtmp_core.h"
#import "rtmp_preview.h"

// Managers
static RTMPCameraManager *cameraManager = nil;
static RTMPPreviewManager *previewManager = nil;

// Estado global
static BOOL isRTMPEnabled = NO;
static BOOL isServerRunning = NO;

// Configurações
static NSString *const kRTMPServerPort = @"1935";
static NSString *const kDefaultStreamKey = @"live";

%hook SpringBoard

- (void)applicationDidFinishLaunching:(id)application {
    %orig;
    
    // Inicializa os managers
    cameraManager = [RTMPCameraManager sharedInstance];
    previewManager = [RTMPPreviewManager sharedInstance];
    
    // Inicia o servidor RTMP
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        [self startRTMPServer];
    });
    
    // Registra para notificações de rede
    [[NSNotificationCenter defaultCenter] addObserver:self
                                           selector:@selector(handleNetworkChange:)
                                               name:@"NetworkStatusChanged"
                                             object:nil];
}

%new
- (void)startRTMPServer {
    if (isServerRunning) return;
    
    @try {
        [cameraManager startServer];
        isServerRunning = YES;
        NSLog(@"[RTMPCamera] Servidor RTMP iniciado na porta %@", kRTMPServerPort);
    } @catch (NSException *exception) {
        NSLog(@"[RTMPCamera] Erro ao iniciar servidor: %@", exception);
    }
}

%new
- (void)stopRTMPServer {
    if (!isServerRunning) return;
    
    @try {
        [cameraManager stopServer];
        isServerRunning = NO;
        NSLog(@"[RTMPCamera] Servidor RTMP parado");
    } @catch (NSException *exception) {
        NSLog(@"[RTMPCamera] Erro ao parar servidor: %@", exception);
    }
}

%new
- (void)handleNetworkChange:(NSNotification *)notification {
    // Reinicia o servidor se necessário
    if (isServerRunning) {
        [self stopRTMPServer];
        [self startRTMPServer];
    }
}

%end

%hook AVCaptureDevice

+ (NSArray<AVCaptureDevice *> *)devicesWithMediaType:(NSString *)mediaType {
    NSArray *originalDevices = %orig;
    
    if (!isRTMPEnabled || ![mediaType isEqualToString:AVMediaTypeVideo]) {
        return originalDevices;
    }
    
    // Cria dispositivo virtual
    AVCaptureDevice *virtualDevice = [[AVCaptureDevice alloc] init];
    // Configura características do dispositivo virtual
    [self configureVirtualDevice:virtualDevice];
    
    // Adiciona o dispositivo virtual à lista
    NSMutableArray *devices = [originalDevices mutableCopy];
    [devices addObject:virtualDevice];
    
    return devices;
}

%new
+ (void)configureVirtualDevice:(AVCaptureDevice *)device {
    // Configura características para parecer uma câmera real
    [device setValue:@"Virtual Camera" forKey:@"localizedName"];
    [device setValue:@"com.rtmpcamera.virtual" forKey:@"uniqueID"];
    [device setValue:@YES forKey:@"hasFlash"];
    [device setValue:@YES forKey:@"hasTorch"];
    [device setValue:@YES forKey:@"focusPointSupported"];
    [device setValue:@YES forKey:@"exposurePointSupported"];
    
    // Configura formatos suportados
    CMVideoDimensions dimensions = {1920, 1080};
    [self setupVideoFormats:device dimensions:dimensions];
}

%new
+ (void)setupVideoFormats:(AVCaptureDevice *)device dimensions:(CMVideoDimensions)dimensions {
    NSMutableArray *formats = [NSMutableArray array];
    
    // Formato H.264
    CMVideoFormatDescriptionRef format;
    CMVideoFormatDescriptionCreate(kCFAllocatorDefault,
                                 kCMVideoCodecType_H264,
                                 dimensions.width,
                                 dimensions.height,
                                 NULL,
                                 &format);
    [formats addObject:(__bridge id)format];
    CFRelease(format);
    
    // Configura os formatos no dispositivo
    [device setValue:formats forKey:@"formats"];
}

%end

%hook AVCaptureSession

- (void)startRunning {
    if (isRTMPEnabled) {
        // Inicia preview do RTMP
        [previewManager startProcessing];
        return;
    }
    %orig;
}

- (void)stopRunning {
    if (isRTMPEnabled) {
        // Para preview do RTMP
        [previewManager stopProcessing];
        return;
    }
    %orig;
}

%end

// Ctor do tweak
%ctor {
    @autoreleasepool {
        // Carrega configurações
        isRTMPEnabled = YES; // Pode ser controlado por preferências
        
        // Registra para notificações de memória baixa
        [[NSNotificationCenter defaultCenter] addObserver:[RTMPCameraManager sharedInstance]
                                                 selector:@selector(handleLowMemory)
                                                     name:UIApplicationDidReceiveMemoryWarningNotification
                                                   object:nil];
    }
}