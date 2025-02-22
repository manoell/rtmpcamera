#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import "rtmp_stream.h"

// Interface principal da camada de compatibilidade
@interface RTMPCameraCompat : NSObject

// Inicializa a camada de compatibilidade
+ (void)initialize;

// Configura o stream RTMP para ser usado como fonte
+ (void)setRTMPStream:(rtmp_stream_t *)stream;

// Habilita/desabilita a substituição da câmera
+ (void)enableCameraReplacement:(BOOL)enable;

// Configura qualidade e características do vídeo
+ (void)configureWithPreset:(NSString *)preset
                resolution:(CGSize)resolution
                      fps:(float)fps;

// Acessa estatísticas e estado
+ (NSDictionary *)getCurrentStats;

@end

// Notificações
extern NSString *const RTMPCameraCompatDidStartNotification;
extern NSString *const RTMPCameraCompatDidStopNotification;
extern NSString *const RTMPCameraCompatErrorNotification;

// Chaves de erro
extern NSString *const RTMPCameraCompatErrorDomain;
extern NSString *const RTMPCameraCompatErrorKey;

#endif // RTMP_CAMERA_COMPAT_H