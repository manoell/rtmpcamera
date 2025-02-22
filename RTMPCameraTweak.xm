#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import <objc/runtime.h>
#import "rtmp_camera_compat.h"
#import "rtmp_preview.h"
#import "rtmp_server_integration.h"
#import "rtmp_diagnostics.h"

// Configurações
static NSString *const kDefaultRTMPServer = @"rtmp://localhost:1935/live";
static NSString *const kDefaultStreamKey = @"stream";
static NSString *const kPrefsPath = @"/var/mobile/Library/Preferences/com.yourdomain.rtmpcamera.plist";

// Chaves de preferências
static NSString *const kEnabledKey = @"Enabled";
static NSString *const kServerURLKey = @"ServerURL";
static NSString *const kStreamKeyKey = @"StreamKey";
static NSString *const kQualityPriorityKey = @"QualityPriority";

// Estado global
static rtmp_server_context_t *gServerContext = NULL;
static RTMPPreviewView *gPreviewView = NULL;
static BOOL gIsEnabled = YES;
static BOOL gSystemReady = NO;
static dispatch_queue_t gRTMPQueue;
static uint32_t gStartTime = 0;

// Funções auxiliares
static BOOL isLocalNetwork(NSString *url) {
    return [url containsString:@"localhost"] || 
           [url containsString:@"127.0.0.1"] || 
           [url containsString:@"192.168."] ||
           [url containsString:@"10."] ||
           [url containsString:@"172.16."];
}

static BOOL isServerReady() {
    if (!gServerContext) return NO;
    
    server_stats_t stats = rtmp_server_get_stats(gServerContext);
    return stats.is_connected && stats.buffer_health > 0.8;
}

static uint32_t getCurrentTime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

// Carrega preferências
static void loadPreferences() {
    NSDictionary *prefs = [NSDictionary dictionaryWithContentsOfFile:kPrefsPath];
    if (prefs) {
        gIsEnabled = [prefs[kEnabledKey] boolValue];
        
        if (gServerContext && gIsEnabled) {
            // Configura servidor com parâmetros otimizados para rede local
            server_config_t config = {
                .max_bitrate = 12000000, // 12 Mbps
                .min_bitrate = 1000000,  // 1 Mbps
                .target_latency = 50,     // 50ms
                .quality_priority = [prefs[kQualityPriorityKey] floatValue] ?: 0.7f,
                .chunk_size = 4096,
                .window_ack_size = 2500000,
                .peer_bandwidth = 2500000,
                .callback = serverEventCallback,
                .callback_ctx = NULL
            };
            rtmp_server_configure(gServerContext, &config);
        }
    }
}

// Callback para eventos do servidor
static void serverEventCallback(server_event_t event, void *data, void *ctx) {
    switch (event) {
        case SERVER_CONNECTED: {
            rtmp_diagnostics_log(LOG_INFO, "Servidor RTMP conectado e pronto");
            uint32_t startup_time = getCurrentTime() - gStartTime;
            rtmp_diagnostics_log(LOG_INFO, "Tempo de inicialização: %dms", startup_time);
            gSystemReady = YES;
            break;
        }
        
        case SERVER_DISCONNECTED: {
            rtmp_diagnostics_log(LOG_WARN, "Servidor RTMP desconectado - Tentando reconexão");
            gSystemReady = NO;
            
            if (gServerContext) {
                dispatch_async(gRTMPQueue, ^{
                    rtmp_server_reconnect(gServerContext);
                });
            }
            break;
        }
        
        case SERVER_ERROR: {
            rtmp_diagnostics_log(LOG_ERROR, "Erro no servidor RTMP: %s", (char*)data);
            break;
        }
        
        case SERVER_STREAM_START: {
            rtmp_diagnostics_log(LOG_INFO, "Stream iniciado com sucesso");
            break;
        }
        
        case SERVER_STREAM_END: {
            rtmp_diagnostics_log(LOG_INFO, "Stream finalizado");
            break;
        }
    }
}

// Inicialização do servidor RTMP
static void initializeRTMPServer() {
    gStartTime = getCurrentTime();
    
    // Cria fila dedicada para operações RTMP
    gRTMPQueue = dispatch_queue_create("com.rtmpcamera.rtmpqueue", 
                                     DISPATCH_QUEUE_SERIAL_WITH_AUTORELEASE_POOL);
    dispatch_set_target_queue(gRTMPQueue, 
                            dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_HIGH, 0));
    
    dispatch_async(gRTMPQueue, ^{
        // Configura e inicia diagnósticos
        diagnostic_config_t diagConfig = {
            .min_log_level = LOG_INFO,
            .qos_enabled = true,
            .failover_enabled = true,
            .buffer_size = 1024 * 1024
        };
        rtmp_diagnostics_init("/var/log/rtmpcamera.log", diagConfig);
        rtmp_diagnostics_log(LOG_INFO, "Iniciando RTMPCamera Tweak...");
        
        // Carrega configurações
        NSDictionary *prefs = [NSDictionary dictionaryWithContentsOfFile:kPrefsPath];
        NSString *serverURL = prefs[kServerURLKey] ?: kDefaultRTMPServer;
        
        // Verifica se é rede local e ajusta configurações
        BOOL isLocal = isLocalNetwork(serverURL);
        rtmp_diagnostics_log(LOG_INFO, "Tipo de rede: %s", isLocal ? "Local" : "Remota");
        
        // Cria contexto do servidor
        gServerContext = rtmp_server_create([serverURL UTF8String], 1935);
        if (!gServerContext) {
            rtmp_diagnostics_log(LOG_ERROR, "Falha ao criar contexto do servidor RTMP");
            return;
        }
        
        // Configura servidor com parâmetros otimizados
        server_config_t config = {
            .chunk_size = 4096,
            .window_ack_size = 2500000,
            .peer_bandwidth = 2500000,
            .max_bitrate = isLocal ? 12000000 : 4000000,
            .min_bitrate = isLocal ? 1000000 : 500000,
            .target_latency = isLocal ? 50 : 100,
            .quality_priority = 0.7f,
            .callback = serverEventCallback,
            .callback_ctx = NULL
        };
        rtmp_server_configure(gServerContext, &config);
        
        // Inicializa preview na thread principal
        dispatch_async(dispatch_get_main_queue(), ^{
            gPreviewView = [[RTMPPreviewView alloc] initWithFrame:CGRectZero];
            gPreviewView.stream = rtmp_server_get_current_stream(gServerContext);
        });
        
        // Conecta ao servidor
        if (rtmp_server_connect(gServerContext) == 0) {
            rtmp_diagnostics_log(LOG_INFO, "Servidor RTMP inicializado - Aguardando conexões");
        } else {
            rtmp_diagnostics_log(LOG_ERROR, "Falha ao conectar servidor RTMP");
        }
    });
}

// Construtor do tweak
%ctor {
    @autoreleasepool {
        loadPreferences();
        if (gIsEnabled) {
            initializeRTMPServer();
        }
    }
}

// Hook para AVCaptureDevice
%hook AVCaptureDevice

+ (AVCaptureDevice *)defaultDeviceWithMediaType:(NSString *)mediaType {
    if (!gIsEnabled || !gSystemReady || ![mediaType isEqualToString:AVMediaTypeVideo]) {
        return %orig;
    }
    
    rtmp_diagnostics_log(LOG_INFO, "Substituindo câmera do sistema por RTMP");
    return (AVCaptureDevice *)[[RTMPCameraCompatLayer alloc] init];
}

%end

// Hook para AVCaptureSession
%hook AVCaptureSession

- (void)startRunning {
    if (!gIsEnabled || !gSystemReady) {
        %orig;
        return;
    }
    
    // Verifica se é sessão de vídeo
    BOOL isVideoSession = NO;
    for (AVCaptureInput *input in self.inputs) {
        if ([input.ports.firstObject.mediaType isEqualToString:AVMediaTypeVideo]) {
            isVideoSession = YES;
            break;
        }
    }
    
    if (!isVideoSession) {
        %orig;
        return;
    }
    
    rtmp_diagnostics_log(LOG_INFO, "Configurando sessão de câmera RTMP");
    
    // Remove inputs existentes
    [self removeAllInputs];
    
    // Configura nossa câmera RTMP
    RTMPCameraCompatLayer *camera = (RTMPCameraCompatLayer *)[AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
    NSError *error = nil;
    AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice:camera error:&error];
    
    if (error) {
        rtmp_diagnostics_log(LOG_ERROR, "Erro ao criar input de câmera: %s", 
                           error.localizedDescription.UTF8String);
        %orig;
        return;
    }
    
    if ([self canAddInput:input]) {
        [self addInput:input];
        rtmp_diagnostics_log(LOG_INFO, "Câmera RTMP configurada com sucesso");
    }
    
    %orig;
}

%end

// Hook para SpringBoard
%hook SpringBoard

- (void)applicationDidFinishLaunching:(id)application {
    %orig;
    
    if (!gIsEnabled) return;
    
    // Registra para mudanças de preferências
    CFNotificationCenterAddObserver(CFNotificationCenterGetDarwinNotifyCenter(),
                                  NULL,
                                  (CFNotificationCallback)loadPreferences,
                                  CFSTR("com.yourdomain.rtmpcamera.prefschanged"),
                                  NULL,
                                  CFNotificationSuspensionBehaviorDeliverImmediately);
    
    rtmp_diagnostics_log(LOG_INFO, "RTMPCamera registrado para notificações de preferências");
}

%end

// Destrutor do tweak
%dtor {
    if (gServerContext) {
        rtmp_server_destroy(gServerContext);
        gServerContext = NULL;
    }
    
    if (gRTMPQueue) {
        dispatch_release(gRTMPQueue);
        gRTMPQueue = NULL;
    }
    
    gPreviewView = nil;
    rtmp_diagnostics_shutdown();
    
    rtmp_diagnostics_log(LOG_INFO, "RTMPCamera Tweak finalizado");
}