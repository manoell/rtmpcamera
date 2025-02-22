#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>

@interface RTMPCameraCompatibility : NSObject

/**
 * Obtém a instância singleton do gerenciador de compatibilidade
 */
+ (instancetype)sharedInstance;

/**
 * Inicia a substituição da câmera do sistema
 * Deve ser chamado quando o tweak é carregado
 */
- (void)startCameraOverride;

/**
 * Para a substituição da câmera do sistema
 * Deve ser chamado quando o tweak é descarregado
 */
- (void)stopCameraOverride;

/**
 * Configura a compatibilidade para um app específico
 * @param bundleId O bundle identifier do app
 */
- (void)setupForApp:(NSString *)bundleId;

/**
 * Define as dimensões padrão do vídeo
 * @param width Largura do vídeo
 * @param height Altura do vídeo
 */
- (void)setDefaultDimensions:(CGSize)dimensions;

/**
 * Verifica se um determinado app está usando a câmera virtual
 * @param bundleId O bundle identifier do app
 * @return YES se o app está usando a câmera virtual, NO caso contrário
 */
- (BOOL)isAppUsingVirtualCamera:(NSString *)bundleId;

/**
 * Obtém as estatísticas de uso da câmera virtual
 * @return Dicionário com estatísticas
 */
- (NSDictionary *)getStats;

@end

// Notificações
extern NSString *const RTMPCameraAppStartedUsingCamera;
extern NSString *const RTMPCameraAppStoppedUsingCamera;
extern NSString *const RTMPCameraFormatChanged;

// Keys para estatísticas
extern NSString *const RTMPCameraStatsActiveApps;
extern NSString *const RTMPCameraStatsCurrentFormat;
extern NSString *const RTMPCameraStatsFrameRate;
extern NSString *const RTMPCameraStatsUptime;