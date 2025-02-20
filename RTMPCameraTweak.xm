// RTMPCameraTweak.xm
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#import "rtmp_core.h"
#import "rtmp_preview.h"

static RTMPServer* rtmpServer = nil;
static RTMPPreviewView* previewView = nil;
static bool isInitialized = false;

static void initializePreview() {
    dispatch_async(dispatch_get_main_queue(), ^{
        rtmp_log(RTMP_LOG_INFO, "Attempting to initialize preview...");
        
        // Espera até ter uma window válida
        UIWindow *window = nil;
        NSArray *windows = [UIApplication sharedApplication].windows;
        for (UIWindow *w in windows) {
            if (w.isKeyWindow) {
                window = w;
                break;
            }
        }
        
        if (!window) {
            rtmp_log(RTMP_LOG_ERROR, "No valid window found, will retry later");
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC), 
                         dispatch_get_main_queue(), ^{
                initializePreview();
            });
            return;
        }
        
        @try {
            CGRect frame = CGRectMake(20, 60, 160, 90);
            previewView = [[RTMPPreviewView alloc] initWithFrame:frame];
            [previewView setupPreviewWithSize:CGSizeMake(1280, 720)];
            previewView.layer.cornerRadius = 8;
            previewView.clipsToBounds = YES;
            
            UIPanGestureRecognizer *pan = [[UIPanGestureRecognizer alloc] 
                initWithTarget:previewView 
                action:@selector(handlePan:)];
            [previewView addGestureRecognizer:pan];
            
            [window addSubview:previewView];
            [window bringSubviewToFront:previewView];
            [previewView startPreview];
            
            rtmp_log(RTMP_LOG_INFO, "Preview view initialized successfully");
        } @catch (NSException *exception) {
            rtmp_log(RTMP_LOG_ERROR, "Exception initializing preview: %s", 
                    [exception.description UTF8String]);
        }
    });
}

static void initializeRTMPServer() {
    if (isInitialized) return;
    isInitialized = true;
    
    rtmp_log(RTMP_LOG_INFO, "Initializing RTMP server...");
    
    // Cria o servidor na porta 1935
    rtmpServer = rtmp_server_create(1935);
    if (!rtmpServer) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to create RTMP server");
        return;
    }
    
    // Inicia o servidor
    int status = rtmp_server_start(rtmpServer);
    if (status != RTMP_OK) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to start RTMP server: %d", status);
        rtmp_server_destroy(rtmpServer);
        rtmpServer = nil;
        return;
    }
    
    rtmp_log(RTMP_LOG_INFO, "RTMP server started successfully on port 1935");
    
    // Aguarda um pouco antes de inicializar o preview
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC), 
                  dispatch_get_main_queue(), ^{
        initializePreview();
    });
}

%ctor {
    @autoreleasepool {
        // Verifica se é o SpringBoard
        NSString *processName = [NSProcessInfo processInfo].processName;
        if (![processName isEqualToString:@"SpringBoard"]) {
            return;
        }
        
        rtmp_log(RTMP_LOG_INFO, "RTMPCamera tweak starting in SpringBoard");
        
        // Aguarda o SpringBoard estar completamente inicializado
        CFNotificationCenterAddObserver(CFNotificationCenterGetDarwinNotifyCenter(),
                                      NULL,
                                      (CFNotificationCallback)initializeRTMPServer,
                                      CFSTR("com.apple.springboard.finishedstartup"),
                                      NULL,
                                      CFNotificationSuspensionBehaviorDeliverImmediately);
        
        %init;
        rtmp_log(RTMP_LOG_INFO, "RTMPCamera tweak initialized in SpringBoard");
    }
}