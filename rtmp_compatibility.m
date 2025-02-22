#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import "rtmp_compatibility.h"

@interface RTMPCompatibilityManager : NSObject

@property (nonatomic, strong) NSMutableDictionary *appProfiles;
@property (nonatomic, strong) NSMutableSet *activeApps;
@property (nonatomic, strong) dispatch_queue_t compatQueue;
@property (nonatomic, assign) CMVideoDimensions defaultDimensions;
@property (nonatomic, assign) BOOL isOverriding;

+ (instancetype)sharedInstance;
- (void)setupCompatibilityForApp:(NSString *)bundleId;
- (void)injectCameraOverrides;
- (void)handleAppStateChange:(NSString *)bundleId state:(RTMPAppState)state;

@end

@implementation RTMPCompatibilityManager

+ (instancetype)sharedInstance {
    static RTMPCompatibilityManager *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[self alloc] init];
    });
    return instance;
}

- (instancetype)init {
    if (self = [super init]) {
        self.appProfiles = [NSMutableDictionary dictionary];
        self.activeApps = [NSMutableSet set];
        self.compatQueue = dispatch_queue_create("com.rtmpcamera.compat", DISPATCH_QUEUE_SERIAL);
        self.defaultDimensions = (CMVideoDimensions){1920, 1080};
        
        [self setupDefaultProfiles];
        [self setupNotifications];
    }
    return self;
}

- (void)setupDefaultProfiles {
    // Perfis de apps populares
    NSArray *defaultProfiles = @[
        @{
            @"bundleId": @"com.apple.camera",
            @"name": @"Camera",
            @"supportedResolutions": @[@"1920x1080", @"1280x720", @"854x480"],
            @"requiresMetadata": @YES,
            @"requiresPreview": @YES,
            @"overrideOrientation": @NO
        },
        @{
            @"bundleId": @"com.google.ios.youtube",
            @"name": @"YouTube",
            @"supportedResolutions": @[@"1920x1080", @"1280x720"],
            @"requiresMetadata": @YES,
            @"requiresPreview": @YES,
            @"overrideOrientation": @YES
        },
        // Adicione mais perfis conforme necessário
    ];
    
    for (NSDictionary *profile in defaultProfiles) {
        [self.appProfiles setObject:profile forKey:profile[@"bundleId"]];
    }
}

- (void)setupNotifications {
    NSNotificationCenter *center = [NSNotificationCenter defaultCenter];
    
    [center addObserver:self
               selector:@selector(handleAppActivation:)
                   name:UIApplicationDidBecomeActiveNotification
                 object:nil];
    
    [center addObserver:self
               selector:@selector(handleAppDeactivation:)
                   name:UIApplicationWillResignActiveNotification
                 object:nil];
}

- (void)setupCompatibilityForApp:(NSString *)bundleId {
    dispatch_async(self.compatQueue, ^{
        NSDictionary *profile = self.appProfiles[bundleId];
        if (!profile) {
            // Criar perfil genérico para apps desconhecidos
            profile = @{
                @"bundleId": bundleId,
                @"name": @"Generic App",
                @"supportedResolutions": @[@"1920x1080", @"1280x720"],
                @"requiresMetadata": @YES,
                @"requiresPreview": @YES,
                @"overrideOrientation": @NO
            };
            [self.appProfiles setObject:profile forKey:bundleId];
        }
        
        [self injectCompatibilityForProfile:profile];
    });
}

- (void)injectCompatibilityForProfile:(NSDictionary *)profile {
    if (![profile[@"bundleId"] length]) return;
    
    // Configurar características da câmera virtual
    AVCaptureDevice *virtualDevice = [self createVirtualDeviceForProfile:profile];
    
    // Injetar sobrescritas específicas do app
    Method originalMethod = class_getInstanceMethod([AVCaptureDevice class], 
                                                  @selector(deviceType));
    Method customMethod = class_getInstanceMethod([self class], 
                                                @selector(customDeviceType));
    method_exchangeImplementations(originalMethod, customMethod);
    
    // Configurar metadados se necessário
    if ([profile[@"requiresMetadata"] boolValue]) {
        [self setupMetadataForDevice:virtualDevice profile:profile];
    }
}

- (AVCaptureDevice *)createVirtualDeviceForProfile:(NSDictionary *)profile {
    // Criar dispositivo virtual com características específicas do app
    AVCaptureDevice *device = [[AVCaptureDevice alloc] init];
    
    // Configurar características básicas
    [device setValue:@"RTMP Virtual Camera" forKey:@"localizedName"];
    [device setValue:[NSUUID UUID].UUIDString forKey:@"uniqueID"];
    
    // Configurar formatos suportados
    NSArray *resolutions = profile[@"supportedResolutions"];
    NSMutableArray *formats = [NSMutableArray array];
    
    for (NSString *resolution in resolutions) {
        NSArray *dimensions = [resolution componentsSeparatedByString:@"x"];
        if (dimensions.count == 2) {
            CMVideoFormatDescriptionRef format;
            int width = [dimensions[0] intValue];
            int height = [dimensions[1] intValue];
            
            CMVideoFormatDescriptionCreate(kCFAllocatorDefault,
                                         kCMVideoCodecType_H264,
                                         width, height,
                                         NULL,
                                         &format);
            
            [formats addObject:(__bridge id)format];
            CFRelease(format);
        }
    }
    
    [device setValue:formats forKey:@"formats"];
    
    return device;
}

- (void)setupMetadataForDevice:(AVCaptureDevice *)device profile:(NSDictionary *)profile {
    // Configurar metadados da câmera
    NSMutableDictionary *metadata = [NSMutableDictionary dictionary];
    
    metadata[@"manufacturer"] = @"Apple";
    metadata[@"model"] = @"iPhone Camera";
    metadata[@"serialNumber"] = [NSUUID UUID].UUIDString;
    metadata[@"firmwareVersion"] = @"1.0.0";
    
    // Adicionar recursos suportados
    metadata[@"supportedFeatures"] = @{
        @"flash": @YES,
        @"torch": @YES,
        @"focusPoint": @YES,
        @"exposurePoint": @YES,
        @"whiteBalance": @YES,
        @"lowLightBoost": @YES
    };
    
    [device setValue:metadata forKey:@"deviceMetadata"];
}

- (void)handleAppStateChange:(NSString *)bundleId state:(RTMPAppState)state {
    dispatch_async(self.compatQueue, ^{
        switch (state) {
            case RTMPAppStateActive:
                [self.activeApps addObject:bundleId];
                [self setupCompatibilityForApp:bundleId];
                break;
                
            case RTMPAppStateInactive:
                [self.activeApps removeObject:bundleId];
                break;
                
            case RTMPAppStateBackground:
                // Manter compatibilidade mas reduzir recursos
                if ([self.activeApps containsObject:bundleId]) {
                    [self optimizeResourcesForBackground:bundleId];
                }
                break;
        }
    });
}

- (void)optimizeResourcesForBackground:(NSString *)bundleId {
    NSDictionary *profile = self.appProfiles[bundleId];
    if (!profile) return;
    
    // Reduzir qualidade/recursos quando em background
    if ([profile[@"requiresPreview"] boolValue]) {
        // Reduzir resolução do preview
        self.defaultDimensions = (CMVideoDimensions){854, 480};
    }
}

@end