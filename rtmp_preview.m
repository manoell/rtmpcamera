#import <UIKit/UIKit.h>
#import <AVFoundation/AVFoundation.h>
#import "rtmp_core.h"

@interface RTMPPreviewView : UIView

@property (nonatomic, strong) AVSampleBufferDisplayLayer *previewLayer;
@property (nonatomic, assign) rtmp_session_t *session;
@property (nonatomic, strong) dispatch_queue_t previewQueue;
@property (nonatomic, assign) BOOL isSetup;

- (void)setupPreviewLayer;
- (void)displayVideoFrame:(CMSampleBufferRef)sampleBuffer;
- (void)cleanupPreview;

@end

@implementation RTMPPreviewView

+ (Class)layerClass {
    return [AVSampleBufferDisplayLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _previewQueue = dispatch_queue_create("com.rtmpcamera.preview", DISPATCH_QUEUE_SERIAL);
        _isSetup = NO;
        [self setupPreviewLayer];
    }
    return self;
}

- (void)setupPreviewLayer {
    if (_isSetup) return;
    
    self.previewLayer = (AVSampleBufferDisplayLayer *)self.layer;
    self.previewLayer.videoGravity = AVLayerVideoGravityResizeAspectFill;
    self.previewLayer.backgroundColor = [[UIColor blackColor] CGColor];
    
    // Configure for low latency
    self.previewLayer.preferredFrameRate = 60;
    if (@available(iOS 13.0, *)) {
        self.previewLayer.preventsDisplaySleepDuringVideoPlayback = YES;
    }
    
    _isSetup = YES;
}

- (void)displayVideoFrame:(CMSampleBufferRef)sampleBuffer {
    if (!_isSetup || !sampleBuffer) return;
    
    CFRetain(sampleBuffer);
    dispatch_async(_previewQueue, ^{
        if (self.previewLayer.status == AVQueuedSampleBufferRenderingStatusFailed) {
            [self.previewLayer flush];
        }
        
        if (self.previewLayer.readyForMoreMediaData) {
            [self.previewLayer enqueueSampleBuffer:sampleBuffer];
        }
        
        CFRelease(sampleBuffer);
    });
}

- (void)cleanupPreview {
    dispatch_async(_previewQueue, ^{
        [self.previewLayer flush];
        self.session = NULL;
        _isSetup = NO;
    });
}

- (void)dealloc {
    [self cleanupPreview];
}

@end

// RTMP Preview Controller
@interface RTMPPreviewController : UIViewController

@property (nonatomic, strong) RTMPPreviewView *previewView;
@property (nonatomic, assign) rtmp_session_t *session;
@property (nonatomic, strong) dispatch_queue_t processingQueue;
@property (nonatomic, assign) BOOL isProcessing;

- (void)startPreview;
- (void)stopPreview;
- (void)processVideoData:(const uint8_t *)data size:(size_t)size timestamp:(uint32_t)timestamp;
- (void)processAudioData:(const uint8_t *)data size:(size_t)size timestamp:(uint32_t)timestamp;

@end

@implementation RTMPPreviewController

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.processingQueue = dispatch_queue_create("com.rtmpcamera.processing", DISPATCH_QUEUE_SERIAL);
    self.isProcessing = NO;
    
    // Setup preview view
    self.previewView = [[RTMPPreviewView alloc] initWithFrame:self.view.bounds];
    self.previewView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:self.previewView];
}

- (void)startPreview {
    self.isProcessing = YES;
    self.previewView.session = self.session;
    [self.previewView setupPreviewLayer];
}

- (void)stopPreview {
    self.isProcessing = NO;
    [self.previewView cleanupPreview];
}

- (void)processVideoData:(const uint8_t *)data size:(size_t)size timestamp:(uint32_t)timestamp {
    if (!self.isProcessing) return;
    
    dispatch_async(self.processingQueue, ^{
        // Decode H.264 video data and create CMSampleBuffer
        CMSampleBufferRef sampleBuffer = [self createSampleBufferFromH264Data:data size:size timestamp:timestamp];
        if (sampleBuffer) {
            [self.previewView displayVideoFrame:sampleBuffer];
            CFRelease(sampleBuffer);
        }
    });
}

- (void)processAudioData:(const uint8_t *)data size:(size_t)size timestamp:(uint32_t)timestamp {
    if (!self.isProcessing) return;
    
    dispatch_async(self.processingQueue, ^{
        // Process AAC audio data if needed
        // Currently focusing on video preview
    });
}

- (CMSampleBufferRef)createSampleBufferFromH264Data:(const uint8_t *)data 
                                              size:(size_t)size 
                                        timestamp:(uint32_t)timestamp {
    // Create format description if needed
    static CMVideoFormatDescriptionRef formatDescription = NULL;
    if (!formatDescription) {
        const uint8_t *parameterSetPointers[2] = {NULL, NULL};
        size_t parameterSetSizes[2] = {0, 0};
        int parameterSetCount = 0;
        
        // Extract SPS and PPS from H.264 stream
        // This is a simplified version - you'll need proper NAL unit parsing
        if ([self extractSPSAndPPS:data size:size
                     parameterSets:parameterSetPointers
                 parameterSetSizes:parameterSetSizes
                            count:&parameterSetCount]) {
            
            CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault,
                                                              parameterSetCount,
                                                              parameterSetPointers,
                                                              parameterSetSizes,
                                                              4,
                                                              &formatDescription);
        }
    }
    
    if (!formatDescription) return NULL;
    
    // Create timing info
    CMTime presentationTime = CMTimeMake(timestamp, 1000); // Convert milliseconds to CMTime
    CMSampleTimingInfo timing = {
        .duration = kCMTimeInvalid,
        .presentationTimeStamp = presentationTime,
        .decodeTimeStamp = presentationTime
    };
    
    // Create sample buffer
    CMSampleBufferRef sampleBuffer = NULL;
    uint8_t *dataBuffer = malloc(size);
    memcpy(dataBuffer, data, size);
    
    CMBlockBufferRef blockBuffer = NULL;
    CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault,
                                     dataBuffer,
                                     size,
                                     kCFAllocatorMalloc,
                                     NULL,
                                     0,
                                     size,
                                     0,
                                     &blockBuffer);
    
    CMSampleBufferCreate(kCFAllocatorDefault,
                        blockBuffer,
                        YES,
                        NULL,
                        NULL,
                        formatDescription,
                        1,
                        1,
                        &timing,
                        0,
                        NULL,
                        &sampleBuffer);
    
    CFRelease(blockBuffer);
    return sampleBuffer;
}

- (BOOL)extractSPSAndPPS:(const uint8_t *)data 
                    size:(size_t)size
            parameterSets:(const uint8_t **)parameterSets
        parameterSetSizes:(size_t *)parameterSetSizes
                   count:(int *)count {
    if (!data || !size || !parameterSets || !parameterSetSizes || !count) return NO;
    
    *count = 0;
    size_t offset = 0;
    
    // Find start codes
    while (offset + 4 <= size) {
        // Look for start code
        if (data[offset] == 0x00 && data[offset + 1] == 0x00 &&
            data[offset + 2] == 0x00 && data[offset + 3] == 0x01) {
            
            // Skip start code
            offset += 4;
            if (offset >= size) break;
            
            // Get NAL unit type
            uint8_t nal_unit_type = data[offset] & 0x1F;
            
            // Find next start code
            size_t next_offset = offset + 1;
            while (next_offset + 3 < size) {
                if (data[next_offset] == 0x00 && data[next_offset + 1] == 0x00 &&
                    ((data[next_offset + 2] == 0x01) ||
                     (data[next_offset + 2] == 0x00 && data[next_offset + 3] == 0x01))) {
                    break;
                }
                next_offset++;
            }
            
            // Calculate NAL unit size
            size_t nal_size = next_offset - offset;
            
            // Store SPS or PPS
            if (nal_unit_type == 7) { // SPS
                if (*count >= 2) break;
                parameterSets[*count] = data + offset;
                parameterSetSizes[*count] = nal_size;
                (*count)++;
            }
            else if (nal_unit_type == 8) { // PPS
                if (*count >= 2) break;
                parameterSets[*count] = data + offset;
                parameterSetSizes[*count] = nal_size;
                (*count)++;
            }
            
            offset = next_offset;
        } else {
            offset++;
        }
    }
    
    return *count > 0;
}

@end