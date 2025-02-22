#import <UIKit/UIKit.h>
#import "rtmp_stream.h"

@interface RTMPPreviewView : UIView

// Stream associado ao preview
@property (nonatomic, assign) rtmp_stream_t *stream;

// Processa um frame de vídeo recebido
- (void)processVideoFrame:(video_frame_t *)frame;

@end