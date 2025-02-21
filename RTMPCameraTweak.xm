#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#import <CoreImage/CoreImage.h>
#import "rtmp_core.h"

static BOOL serverInitialized = NO;
static RTMPServer* rtmpServer = nil;

// Janela de Preview
@interface RTMPPreviewWindow : UIWindow
@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, strong) UIImageView *previewImageView;
@property (nonatomic, strong) UILabel *statsLabel;
@end

@implementation RTMPPreviewWindow

- (instancetype)init {
    self = [super initWithFrame:CGRectMake(20, 60, 160, 90)];
    if (self) {
        self.windowLevel = UIWindowLevelAlert + 1;
        self.backgroundColor = [UIColor blackColor];
        self.layer.cornerRadius = 8;
        self.clipsToBounds = YES;
        
        // Preview ImageView
        self.previewImageView = [[UIImageView alloc] initWithFrame:CGRectMake(0, 0, self.frame.size.width, 70)];
        self.previewImageView.contentMode = UIViewContentModeScaleAspectFit;
        self.previewImageView.backgroundColor = [UIColor clearColor];
        [self addSubview:self.previewImageView];
        
        // Status label
        self.statusLabel = [[UILabel alloc] initWithFrame:CGRectMake(0, 70, self.frame.size.width, 10)];
        self.statusLabel.textColor = [UIColor whiteColor];
        self.statusLabel.textAlignment = NSTextAlignmentCenter;
        self.statusLabel.font = [UIFont systemFontOfSize:8];
        self.statusLabel.text = @"Aguardando conexão RTMP...";
        [self addSubview:self.statusLabel];
        
        // Stats label
        self.statsLabel = [[UILabel alloc] initWithFrame:CGRectMake(0, 80, self.frame.size.width, 10)];
        self.statsLabel.textColor = [UIColor whiteColor];
        self.statsLabel.textAlignment = NSTextAlignmentCenter;
        self.statsLabel.font = [UIFont systemFontOfSize:8];
        self.statsLabel.text = @"0 KB/s | 0 FPS";
        [self addSubview:self.statsLabel];
        
        // Pan gesture
        UIPanGestureRecognizer *pan = [[UIPanGestureRecognizer alloc] 
            initWithTarget:self action:@selector(handlePan:)];
        [self addGestureRecognizer:pan];
        
        rtmp_log(RTMP_LOG_INFO, "Preview window initialized");
    }
    return self;
}

- (void)handlePan:(UIPanGestureRecognizer *)gesture {
    CGPoint translation = [gesture translationInView:self];
    self.center = CGPointMake(self.center.x + translation.x, 
                             self.center.y + translation.y);
    [gesture setTranslation:CGPointZero inView:self];
}

- (void)updateStatus:(NSString *)status {
    dispatch_async(dispatch_get_main_queue(), ^{
        self.statusLabel.text = status;
    });
}

- (void)updateStats:(NSString *)stats {
    dispatch_async(dispatch_get_main_queue(), ^{
        self.statsLabel.text = stats;
    });
}

- (void)updatePreviewImage:(UIImage *)image {
    dispatch_async(dispatch_get_main_queue(), ^{
        self.previewImageView.image = image;
    });
}

@end

static RTMPPreviewWindow* previewWindow = nil;

%hook SpringBoard

- (void)applicationDidFinishLaunching:(id)application {
    %orig;
    
    if (serverInitialized) return;
    serverInitialized = YES;
    
    rtmp_log(RTMP_LOG_INFO, "Initializing RTMP server...");
    
    // Inicializar servidor RTMP
    rtmpServer = rtmp_server_create(1935);
    if (!rtmpServer) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to create RTMP server");
        return;
    }
    
    int status = rtmp_server_start(rtmpServer);
    if (status != RTMP_OK) {
        rtmp_log(RTMP_LOG_ERROR, "Failed to start RTMP server: %d", status);
        rtmp_server_destroy(rtmpServer);
        rtmpServer = nil;
        return;
    }
    
    rtmp_log(RTMP_LOG_INFO, "RTMP server started successfully");
    
    // Criar e mostrar preview window
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), 
                  dispatch_get_main_queue(), ^{
        if (!previewWindow) {
            previewWindow = [[RTMPPreviewWindow alloc] init];
            [previewWindow makeKeyAndVisible];
            rtmp_log(RTMP_LOG_INFO, "Preview window created and displayed");
        }
    });
}

%end

%ctor {
    @autoreleasepool {
        NSString *processName = [NSProcessInfo processInfo].processName;
        if ([processName isEqualToString:@"SpringBoard"]) {
            rtmp_log(RTMP_LOG_INFO, "RTMPCamera tweak initialized in SpringBoard");
            %init;
        }
    }
}