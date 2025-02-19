#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <UIKit/UIKit.h>
#import <CoreImage/CoreImage.h>
#import <arpa/inet.h>
#import <netinet/in.h>
#import <sys/socket.h>
#import <time.h>
#import <stdarg.h>

static BOOL isServerRunning = NO;
static NSDateFormatter *logDateFormatter;

static void writeToLog(NSString *format, ...) {
    NSString *logPath = @"/var/tmp/rtmp_debug.log";
    
    // Inicializar formatador de data uma única vez
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        logDateFormatter = [[NSDateFormatter alloc] init];
        [logDateFormatter setDateFormat:@"yyyy-MM-dd HH:mm:ss"];
    });
    
    // Formatar mensagem com argumentos variáveis
    va_list args;
    va_start(args, format);
    NSString *message = [[NSString alloc] initWithFormat:format arguments:args];
    va_end(args);
    
    // Formatar mensagem com data e hora
    NSString *dateString = [logDateFormatter stringFromDate:[NSDate date]];
    NSString *formattedMessage = [NSString stringWithFormat:@"[%@] %@\n", dateString, message];
    
    // Escrever no arquivo
    NSFileHandle *fileHandle = [NSFileHandle fileHandleForWritingAtPath:logPath];
    if (!fileHandle) {
        [@"" writeToFile:logPath atomically:YES encoding:NSUTF8StringEncoding error:nil];
        fileHandle = [NSFileHandle fileHandleForWritingAtPath:logPath];
    }
    
    [fileHandle seekToEndOfFile];
    [fileHandle writeData:[formattedMessage dataUsingEncoding:NSUTF8StringEncoding]];
    [fileHandle closeFile];
}

// Definição de tipos RTMP
#define RTMP_MSG_CHUNK_SIZE     1
#define RTMP_MSG_ABORT          2
#define RTMP_MSG_ACK            3
#define RTMP_MSG_USER_CONTROL   4
#define RTMP_MSG_WINDOW_ACK_SIZE 5
#define RTMP_MSG_SET_PEER_BW    6
#define RTMP_MSG_AUDIO          8
#define RTMP_MSG_VIDEO          9
#define RTMP_MSG_AMF3_DATA      15
#define RTMP_MSG_AMF3_SHARED    16
#define RTMP_MSG_AMF3_COMMAND   17
#define RTMP_MSG_AMF0_DATA      18
#define RTMP_MSG_AMF0_SHARED    19
#define RTMP_MSG_AMF0_COMMAND   20

#define RTMP_STATE_INIT         0
#define RTMP_STATE_HANDSHAKE_C2 1
#define RTMP_STATE_CONNECTED    2
#define RTMP_STATE_STREAMING    3

// Para rastrear pacotes fragmentados
typedef struct {
    uint8_t type;
    uint32_t size;
    uint32_t received;
    uint8_t* data;
    uint32_t timestamp;
    uint32_t stream_id;
} rtmp_packet_buffer_t;

// Estrutura de sessão RTMP
typedef struct {
    int socket;
    struct sockaddr_in addr;
    int state;
    int connected;
    uint8_t *in_buffer;
    uint32_t in_buffer_size;
    uint8_t *out_buffer;
    uint32_t out_buffer_size;
    uint32_t in_chunk_size;
    uint32_t out_chunk_size;
    uint32_t window_size;
    uint32_t bytes_in;
    uint32_t bytes_out;
    uint32_t last_ack;
    int preview_enabled;
    void *preview_data;
    
    // Gerenciamento de fragmentos
    rtmp_packet_buffer_t *current_packet;
} rtmp_session_t;

@interface RTMPStatusView : UIView
@property (nonatomic, strong) UILabel *statusLabel;
@property (nonatomic, strong) UILabel *statsLabel;
@property (nonatomic, strong) UIImageView *previewImageView;
- (void)updateStatus:(NSString *)status;
- (void)updateStats:(NSString *)stats;
- (void)updatePreviewFrame:(CVImageBufferRef)imageBuffer;
@end

@implementation RTMPStatusView

- (instancetype)initWithFrame:(CGRect)frame {
    if (self = [super initWithFrame:frame]) {
        self.layer.cornerRadius = 10;
        self.layer.masksToBounds = YES;
        self.backgroundColor = [UIColor colorWithWhite:0 alpha:0.8];
        
        // Preview
        self.previewImageView = [[UIImageView alloc] initWithFrame:CGRectMake(5, 5, frame.size.width-10, frame.size.height-60)];
        self.previewImageView.contentMode = UIViewContentModeScaleAspectFit;
        self.previewImageView.layer.cornerRadius = 5;
        self.previewImageView.layer.masksToBounds = YES;
        self.previewImageView.backgroundColor = [UIColor colorWithWhite:0.1 alpha:1.0];
        [self addSubview:self.previewImageView];
        
        // Status
        self.statusLabel = [[UILabel alloc] initWithFrame:CGRectMake(5, frame.size.height-55, frame.size.width-10, 25)];
        self.statusLabel.textColor = [UIColor whiteColor];
        self.statusLabel.font = [UIFont systemFontOfSize:12];
        self.statusLabel.textAlignment = NSTextAlignmentCenter;
        [self addSubview:self.statusLabel];
        
        // Stats
        self.statsLabel = [[UILabel alloc] initWithFrame:CGRectMake(5, frame.size.height-30, frame.size.width-10, 25)];
        self.statsLabel.textColor = [UIColor whiteColor];
        self.statsLabel.font = [UIFont systemFontOfSize:10];
        self.statsLabel.textAlignment = NSTextAlignmentCenter;
        [self addSubview:self.statsLabel];
    }
    return self;
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

- (void)updatePreviewFrame:(CVImageBufferRef)imageBuffer {
    if (!imageBuffer) return;
    
    CIImage *ciImage = [CIImage imageWithCVPixelBuffer:imageBuffer];
    if (ciImage) {
        dispatch_async(dispatch_get_main_queue(), ^{
            self.previewImageView.image = [UIImage imageWithCIImage:ciImage];
        });
    }
}
@end

@interface RTMPCameraManager : NSObject {
    rtmp_session_t *_rtmpSession;
    dispatch_queue_t _rtmpQueue;
    dispatch_source_t _statsTimer;
    int _serverSocket;
    uint32_t _frameCount;
    uint32_t _byteCount;
    CFAbsoluteTime _lastStatsUpdate;
}

@property (nonatomic, strong) RTMPStatusView *statusView;
@property (nonatomic, assign) CMSampleBufferRef currentBuffer;
@property (nonatomic, assign) BOOL isReceivingStream;
@property (nonatomic, assign) double currentFPS;
@property (nonatomic, assign) double currentBitrate;

+ (instancetype)sharedInstance;
- (void)startRTMPServer;
- (void)setupPreviewWindow;
- (void)acceptClients;
- (void)handleClientSession:(rtmp_session_t *)session;
- (void)updateStats;
- (void)handlePan:(UIPanGestureRecognizer *)gesture;
- (CMSampleBufferRef)createSampleBufferFromH264Data:(uint8_t *)data size:(uint32_t)size timestamp:(uint32_t)timestamp;
- (void)sendConnectResponse:(rtmp_session_t*)session;
- (void)sendCreateStreamResponse:(rtmp_session_t*)session;
- (void)sendPublishResponse:(rtmp_session_t*)session;
- (void)sendPlayResponse:(rtmp_session_t*)session;
- (void)processFragmentedPacket:(rtmp_session_t*)session headerType:(uint8_t)headerType messageType:(uint8_t)messageType 
                     messageSize:(uint32_t)messageSize timestamp:(uint32_t)timestamp streamId:(uint32_t)streamId 
                             data:(uint8_t*)data dataSize:(uint32_t)dataSize;
@end

@implementation RTMPCameraManager

+ (instancetype)sharedInstance {
    static RTMPCameraManager *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[self alloc] init];
    });
    return instance;
}

- (instancetype)init {
    if (self = [super init]) {
        _rtmpQueue = dispatch_queue_create("com.rtmpcamera.queue", DISPATCH_QUEUE_SERIAL);
        _serverSocket = -1;
        _frameCount = 0;
        _byteCount = 0;
        _lastStatsUpdate = CFAbsoluteTimeGetCurrent();
        
        // Configurar timer para estatísticas
        _statsTimer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
        dispatch_source_set_timer(_statsTimer, DISPATCH_TIME_NOW, 1.0 * NSEC_PER_SEC, 0.1 * NSEC_PER_SEC);
        dispatch_source_set_event_handler(_statsTimer, ^{
            [self updateStats];
        });
        dispatch_resume(_statsTimer);
    }
    return self;
}

- (void)dealloc {
    if (_currentBuffer) {
        CFRelease(_currentBuffer);
    }
}

- (void)startRTMPServer {
    dispatch_async(_rtmpQueue, ^{
        writeToLog(@"Iniciando servidor RTMP...");
        
        // Criar socket do servidor
        _serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (_serverSocket < 0) {
            writeToLog(@"Erro: Falha ao criar socket do servidor");
            [self.statusView updateStatus:@"Erro: Falha ao criar socket"];
            return;
        }
        
        // Permitir reuso do endereço
        int enable = 1;
        setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int));
        
        // Configurar endereço
        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = INADDR_ANY;
        serverAddr.sin_port = htons(1935);
        
        // Bind
        if (bind(_serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            writeToLog(@"Erro: Falha ao fazer bind na porta 1935");
            [self.statusView updateStatus:@"Erro: Falha ao fazer bind"];
            close(_serverSocket);
            _serverSocket = -1;
            return;
        }
        
        // Listen
        if (listen(_serverSocket, 5) < 0) {
            writeToLog(@"Erro: Falha ao fazer listen");
            [self.statusView updateStatus:@"Erro: Falha ao fazer listen"];
            close(_serverSocket);
            _serverSocket = -1;
            return;
        }
        
        isServerRunning = YES;
        [self.statusView updateStatus:@"Servidor RTMP iniciado na porta 1935"];
        writeToLog(@"Servidor RTMP iniciado com sucesso na porta 1935");
        
        // Iniciar thread para aceitar clientes
        [self acceptClients];
    });
}

- (void)acceptClients {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        if (_serverSocket < 0) {
            writeToLog(@"Erro: Socket do servidor inválido");
            return;
        }
        
        writeToLog(@"Aguardando conexões...");
        [self.statusView updateStatus:@"Aguardando conexões RTMP..."];
        
        while (isServerRunning) {
            struct sockaddr_in clientAddr;
            socklen_t addrLen = sizeof(clientAddr);
            
            // Aceitar nova conexão
            int clientSocket = accept(_serverSocket, (struct sockaddr*)&clientAddr, &addrLen);
            if (clientSocket < 0) {
                // Não é um erro crítico, apenas continuar aguardando
                usleep(100000); // 100ms
                continue;
            }
            
            // Log da conexão
            char clientIP[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));
            writeToLog(@"Nova conexão de %s:%d", clientIP, ntohs(clientAddr.sin_port));
            
            // Criar sessão RTMP
            rtmp_session_t *session = (rtmp_session_t*)calloc(1, sizeof(rtmp_session_t));
            if (!session) {
                writeToLog(@"Erro: Falha ao alocar memória para sessão");
                close(clientSocket);
                continue;
            }
            
            // Inicializar sessão
            session->socket = clientSocket;
            session->addr = clientAddr;
            session->state = RTMP_STATE_INIT;
            session->connected = 1;
            session->in_chunk_size = 128;
            session->out_chunk_size = 4096; // Valor padrão do Node Media Server
            session->window_size = 2500000;
            session->last_ack = 0;
            session->bytes_in = 0;
            session->bytes_out = 0;
            session->preview_enabled = 1;
            session->current_packet = NULL;
            
            // Alocar buffers
            session->in_buffer = (uint8_t*)malloc(session->window_size);
            session->out_buffer = (uint8_t*)malloc(session->window_size);
            
            if (!session->in_buffer || !session->out_buffer) {
                writeToLog(@"Erro: Falha ao alocar buffers");
                free(session->in_buffer);
                free(session->out_buffer);
                free(session);
                close(clientSocket);
                continue;
            }
            
            session->in_buffer_size = 0;
            session->out_buffer_size = 0;
            
            [self.statusView updateStatus:@"Cliente conectado - Handshake..."];
            
            // Iniciar processamento da sessão em nova thread
            dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
                [self handleClientSession:session];
            });
        }
    });
}

- (void)handleClientSession:(rtmp_session_t *)session {
    if (!session) return;
    
    _rtmpSession = session;
    writeToLog(@"Iniciando processamento da sessão RTMP");
    
    // Definir um timeout mais longo para a conexão
    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(session->socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    // Loop principal de processamento
    uint8_t buffer[16384]; // Buffer maior para comandos maiores (16KB)
    int bytesRead;
    uint32_t lastKeepAliveTime = (uint32_t)time(NULL);
    
    while (session->connected) {
        // Enviar keep-alive periodicamente (a cada 5 segundos)
        uint32_t currentTime = (uint32_t)time(NULL);
        if (currentTime - lastKeepAliveTime > 5) {
            // Enviar ping
            uint8_t pingData[14] = {
                0x02, // Header
                0x00, 0x00, 0x00, // Timestamp
                0x00, 0x00, 0x06, // Message length (6 bytes)
                0x04, // Message type (4 = User Control Message)
                0x00, 0x00, 0x00, 0x00, // Stream ID
                // Ping data (apenas 2 bytes)
                0x00, 0x06 // Ping type
            };
            send(session->socket, pingData, sizeof(pingData), 0);
            writeToLog(@"Ping enviado (keep-alive)");
            lastKeepAliveTime = currentTime;
        }
        
        // Usar select para verificar se há dados disponíveis (não-bloqueante)
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(session->socket, &readfds);
        
        struct timeval timeout;
        timeout.tv_sec = 1;  // Verificar a cada 1 segundo
        timeout.tv_usec = 0;
        
        int result = select(session->socket + 1, &readfds, NULL, NULL, &timeout);
        if (result < 0) {
            writeToLog(@"Erro em select(): %s", strerror(errno));
            break;
        }
        
        if (result == 0) {
            // Timeout, nenhum dado disponível, continuar loop
            continue;
        }
        
        // Ler dados disponíveis
        bytesRead = recv(session->socket, buffer, sizeof(buffer), 0);
		if (bytesRead > 0) {
            session->bytes_in += bytesRead;
            
            // Despejo hexadecimal para debug (apenas os primeiros bytes)
            if (bytesRead > 0) {
                NSMutableString *hexDump = [NSMutableString string];
                int maxBytes = bytesRead > 32 ? 32 : bytesRead;
                for (int i = 0; i < maxBytes; i++) {
                    [hexDump appendFormat:@"%02X ", buffer[i]];
                }
                writeToLog(@"Recebido %d bytes: %@", bytesRead, hexDump);
            }
            
            // Processar handshake se necessário
            if (session->state == RTMP_STATE_INIT) {
                // Handshake C0/C1
                if (bytesRead >= 1537) { // 1 byte C0 + 1536 bytes C1
                    // Verificar versão RTMP (C0)
                    if (buffer[0] != 3) {
                        writeToLog(@"Versão RTMP não suportada: %d", buffer[0]);
                        break;
                    }
                    
                    // Responder com S0+S1+S2
                    uint8_t response[1 + 1536 + 1536];
                    response[0] = 3; // S0 - versão
                    
                    // S1 - timestamp e zeros
                    uint32_t timestamp = (uint32_t)time(NULL);
                    memcpy(response + 1, &timestamp, 4);
                    memset(response + 5, 0, 4);
                    
                    // Resto de S1 aleatório
                    for (int i = 9; i < 1537; i++) {
                        response[i] = rand() % 256;
                    }
                    
                    // S2 - eco do C1
                    memcpy(response + 1537, buffer + 1, 1536);
                    
                    // Enviar resposta
                    send(session->socket, response, sizeof(response), 0);
                    session->state = RTMP_STATE_HANDSHAKE_C2;
                    writeToLog(@"Handshake S0/S1/S2 enviado");
                }
            } 
            else if (session->state == RTMP_STATE_HANDSHAKE_C2) {
                // Recebendo C2
                if (bytesRead >= 1536) {
                    session->state = RTMP_STATE_CONNECTED;
                    writeToLog(@"Handshake completo - Conexão estabelecida");
                    [self.statusView updateStatus:@"Conexão RTMP estabelecida"];
                    self.isReceivingStream = YES;
                    
                    // Enviar nossa configuração de chunk size (importante)
                    uint8_t chunkSizeMsg[16] = {
                        0x02, // Header
                        0x00, 0x00, 0x00, // Timestamp
                        0x00, 0x00, 0x04, // Message length (4 bytes)
                        0x01, // Message type (1 = Set Chunk Size)
                        0x00, 0x00, 0x00, 0x00, // Stream ID
                        // Tamanho do chunk (4096 - valor comum)
                        0x00, 0x00, 0x10, 0x00 // 4096 em big-endian
                    };
                    send(session->socket, chunkSizeMsg, sizeof(chunkSizeMsg), 0);
                    session->out_chunk_size = 4096;
                    writeToLog(@"Enviado Set Chunk Size: 4096");
                    
                    // Pequeno atraso para simular comportamento do Node Media Server
                    usleep(20000); // 20ms
                    
                    // Window Acknowledgement Size
                    uint8_t windowAckMsg[16] = {
                        0x02, // Header (chunk stream ID 2)
                        0x00, 0x00, 0x00, // Timestamp
                        0x00, 0x00, 0x04, // Message length (4 bytes)
                        0x05, // Message type (5 = Window Acknowledgement Size)
                        0x00, 0x00, 0x00, 0x00, // Stream ID
                        // Dados
                        (session->window_size >> 24) & 0xFF,
                        (session->window_size >> 16) & 0xFF,
                        (session->window_size >> 8) & 0xFF,
                        session->window_size & 0xFF
                    };
                    
                    send(session->socket, windowAckMsg, sizeof(windowAckMsg), 0);
                    writeToLog(@"Enviado Window Acknowledgement Size");
                    
                    // Pequeno atraso entre mensagens (alguns clientes precisam)
                    usleep(20000); // 20ms
                    
                    // Set Peer Bandwidth
                    uint8_t peerBwMsg[17] = {
                        0x02, // Header
                        0x00, 0x00, 0x00, // Timestamp
                        0x00, 0x00, 0x05, // Message length (5 bytes)
                        0x06, // Message type (6 = Set Peer Bandwidth)
                        0x00, 0x00, 0x00, 0x00, // Stream ID
                        // Dados
                        (session->window_size >> 24) & 0xFF,
                        (session->window_size >> 16) & 0xFF,
                        (session->window_size >> 8) & 0xFF,
                        session->window_size & 0xFF,
                        0x02 // Dynamic
                    };
                    
                    send(session->socket, peerBwMsg, sizeof(peerBwMsg), 0);
                    writeToLog(@"Enviado Set Peer Bandwidth");
                    
                    // Agora aguardamos pelo comando 'connect' do cliente
                    [self.statusView updateStatus:@"Aguardando comando connect..."];
                }
            }
			else if (session->state == RTMP_STATE_CONNECTED || session->state == RTMP_STATE_STREAMING) {
                // Processar pacotes RTMP
                // Analisar tipo de pacote
                int offset = 0;
                while (offset + 1 <= bytesRead) {
                    // Cabeçalho básico
                    uint8_t basicHeader = buffer[offset];
                    uint8_t headerType = basicHeader >> 6;
                    //uint8_t chunkStreamId = basicHeader & 0x3F;
                    offset++;
                    
                    // Se não tivermos dados suficientes, sair do loop
                    if (offset >= bytesRead) break;
                    
                    // Variáveis para armazenar informações do pacote
                    uint32_t timestamp = 0;
                    uint32_t messageLength = 0;
                    uint8_t messageType = 0;
                    uint32_t streamId = 0;
                    
                    // Decodificar timestamp
                    if (headerType <= 2) {
                        if (offset + 3 > bytesRead) break;
                        timestamp = (buffer[offset] << 16) | (buffer[offset+1] << 8) | buffer[offset+2];
                        offset += 3;
                    }
                    
                    // Decodificar tamanho da mensagem e tipo
                    if (headerType <= 1) {
                        if (offset + 4 > bytesRead) break;
                        messageLength = (buffer[offset] << 16) | (buffer[offset+1] << 8) | buffer[offset+2];
                        messageType = buffer[offset+3];
                        offset += 4;
                    }
                    
                    // Decodificar stream ID
                    if (headerType == 0) {
                        if (offset + 4 > bytesRead) break;
                        streamId = (buffer[offset] << 24) | (buffer[offset+1] << 16) | 
                                  (buffer[offset+2] << 8) | buffer[offset+3];
                        offset += 4;
                    }
                    
                    // Verificar se temos dados suficientes
                    if (offset + messageLength > bytesRead) {
                        // Pacote fragmentado, armazenar em buffer
                        if (messageLength > 0) {
                            [self processFragmentedPacket:session 
                                              headerType:headerType 
                                             messageType:messageType 
                                             messageSize:messageLength 
                                              timestamp:timestamp 
                                               streamId:streamId 
                                                   data:buffer + offset 
                                               dataSize:(bytesRead - offset)];
                        }
                        break;
                    }
                    
                    writeToLog(@"Pacote RTMP: tipo=%d, tamanho=%d, timestamp=%d", 
                               messageType, messageLength, timestamp);
                    
                    // Processar diferentes tipos de mensagem
                    if (messageType == RTMP_MSG_CHUNK_SIZE) {
                        // Atualizar tamanho do chunk
                        if (messageLength >= 4) {
                            uint32_t newChunkSize = (buffer[offset] << 24) | (buffer[offset+1] << 16) | 
                                                  (buffer[offset+2] << 8) | buffer[offset+3];
                            session->in_chunk_size = newChunkSize;
                            writeToLog(@"Cliente definiu novo chunk size: %d", newChunkSize);
                        }
                    }
                    else if (messageType == RTMP_MSG_AMF0_COMMAND && messageLength > 0) {
                        // Verificar comandos AMF0
                        if (buffer[offset] == 0x02) { // String marker
                            uint16_t strLen = 0;
                            if (offset + 3 <= bytesRead) {
                                strLen = (buffer[offset+1] << 8) | buffer[offset+2];
                                if (offset + 3 + strLen <= bytesRead) {
                                    char cmdName[128] = {0};
                                    memcpy(cmdName, buffer + offset + 3, strLen < 127 ? strLen : 127);
                                    writeToLog(@"Comando AMF recebido: %s", cmdName);
                                    
                                    // Enviar respostas apropriadas
                                    if (strcmp(cmdName, "connect") == 0) {
                                        // Pequena pausa para que o cliente processe
                                        usleep(10000); // 10ms
                                        [self sendConnectResponse:session];
                                    }
                                    else if (strcmp(cmdName, "createStream") == 0) {
                                        usleep(10000); // 10ms
                                        [self sendCreateStreamResponse:session];
                                    }
                                    else if (strcmp(cmdName, "publish") == 0 || 
                                             strcmp(cmdName, "FCPublish") == 0) {
                                        usleep(10000); // 10ms
                                        [self sendPublishResponse:session];
                                    }
                                    else if (strcmp(cmdName, "play") == 0) {
                                        usleep(10000); // 10ms
                                        [self sendPlayResponse:session];
                                    }
                                    else if (strcmp(cmdName, "releaseStream") == 0 ||
                                             strcmp(cmdName, "FCUnpublish") == 0 ||
                                             strcmp(cmdName, "deleteStream") == 0) {
                                        // Apenas log para estes comandos
                                        writeToLog(@"Comando ignorado: %s", cmdName);
                                    }
                                }
                            }
                        }
                    }
					else if (messageType == RTMP_MSG_VIDEO) {
                        [self processVideoData:buffer + offset length:messageLength timestamp:timestamp];
                        
                        // Incrementar contadores para estatísticas
                        _frameCount++;
						_byteCount += messageLength;
                        
                        [self.statusView updateStatus:@"Recebendo vídeo..."];
                    }
                    else if (messageType == RTMP_MSG_AUDIO) {
                        // Processar áudio se necessário
                        writeToLog(@"Recebido pacote de áudio: %d bytes", messageLength);
                    }
                    
                    // Avançar para o próximo pacote
                    offset += messageLength;
                }
            }
        } 
        else if (bytesRead == 0) {
            writeToLog(@"Conexão fechada pelo cliente (estado: %d)", session->state);
            break;
        } 
        else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Timeout da leitura, não é um erro real
                continue;
            }
            writeToLog(@"Erro ao ler dados: %s (erro: %d)", strerror(errno), errno);
            break;
        }
    }
    
    // Limpeza
    writeToLog(@"Encerrando sessão RTMP");
    [self.statusView updateStatus:@"Conexão RTMP encerrada"];
    
    self.isReceivingStream = NO;
    
    // Limpar buffer de pacote fragmentado se existir
    if (session->current_packet) {
        if (session->current_packet->data) {
            free(session->current_packet->data);
        }
        free(session->current_packet);
    }
    
    close(session->socket);
    free(session->in_buffer);
    free(session->out_buffer);
    free(session);
    _rtmpSession = NULL;
}

- (void)processFragmentedPacket:(rtmp_session_t*)session
                     headerType:(uint8_t)headerType
                    messageType:(uint8_t)messageType
                    messageSize:(uint32_t)messageSize
                     timestamp:(uint32_t)timestamp
                      streamId:(uint32_t)streamId
                          data:(uint8_t*)data
                      dataSize:(uint32_t)dataSize {
    
    // Se já temos um pacote em andamento
    if (session->current_packet) {
        // Verificar se tipos correspondem
        if (session->current_packet->type == messageType) {
            // Adicionar dados ao pacote existente
            uint32_t remainingSize = session->current_packet->size - session->current_packet->received;
            uint32_t bytesToCopy = (dataSize < remainingSize) ? dataSize : remainingSize;
            
            memcpy(session->current_packet->data + session->current_packet->received, data, bytesToCopy);
            session->current_packet->received += bytesToCopy;
            
            // Verificar se completou o pacote
            if (session->current_packet->received >= session->current_packet->size) {
                // Processar pacote completo
                if (messageType == RTMP_MSG_VIDEO) {
                    [self processVideoData:session->current_packet->data 
                                  length:session->current_packet->size 
                              timestamp:session->current_packet->timestamp];
                    
                    _frameCount++;
                    _byteCount += session->current_packet->size;
                    [self.statusView updateStatus:@"Recebendo vídeo (fragmentado)..."];
                }
                
                // Limpar
                free(session->current_packet->data);
                free(session->current_packet);
                session->current_packet = NULL;
            }
        } else {
            // Tipo diferente, descartar pacote anterior
            free(session->current_packet->data);
            free(session->current_packet);
            session->current_packet = NULL;
        }
    }
    
    // Se não temos um pacote em andamento ou descartamos, criar novo
    if (!session->current_packet && messageSize > 0) {
        session->current_packet = (rtmp_packet_buffer_t*)malloc(sizeof(rtmp_packet_buffer_t));
        if (session->current_packet) {
            session->current_packet->type = messageType;
            session->current_packet->size = messageSize;
            session->current_packet->received = dataSize;
            session->current_packet->timestamp = timestamp;
            session->current_packet->stream_id = streamId;
            
            // Alocar buffer para dados
            session->current_packet->data = (uint8_t*)malloc(messageSize);
            if (session->current_packet->data) {
                memcpy(session->current_packet->data, data, dataSize);
            } else {
                // Falha ao alocar, limpar
                free(session->current_packet);
                session->current_packet = NULL;
            }
        }
    }
}

- (void)processVideoData:(uint8_t *)data length:(uint32_t)length timestamp:(uint32_t)timestamp {
    // Criar sample buffer para preview
    CMSampleBufferRef sampleBuffer = [self createSampleBufferFromH264Data:data size:length timestamp:timestamp];
    if (sampleBuffer) {
        // Atualizar preview
        CVImageBufferRef imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer);
        [self.statusView updatePreviewFrame:imageBuffer];
        
        // Armazenar para interceptação da câmera
        if (self.currentBuffer) {
            CFRelease(self.currentBuffer);
        }
        self.currentBuffer = (CMSampleBufferRef)CFRetain(sampleBuffer);
        
        CFRelease(sampleBuffer);
    }
}

- (void)sendConnectResponse:(rtmp_session_t*)session {
    // Baseado no formato de resposta do Node Media Server
    uint8_t connectResult[] = {
        // Header
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00,
        // AMF string: "_result"
        0x02, 0x00, 0x07, '_', 'r', 'e', 's', 'u', 'l', 't',
        // Transaction ID (number)
        0x00, 0x3f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // Object start
        0x03,
        // "fmsVer" property
        0x00, 0x06, 'f', 'm', 's', 'V', 'e', 'r',
        0x02, 0x00, 0x0d, 'F', 'M', 'S', '/', '3', ',', '5', ',', '1', ',', '5', '2', '5',
        // "capabilities" property
        0x00, 0x0c, 'c', 'a', 'p', 'a', 'b', 'i', 'l', 'i', 't', 'i', 'e', 's',
        0x00, 0x40, 0x2e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Number: 127
        // "mode" property
        0x00, 0x04, 'm', 'o', 'd', 'e',
        0x00, 0x3f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Number: 1
        // Object end
        0x00, 0x00, 0x09,
        // Info object start
        0x03,
        // "level" property
        0x00, 0x05, 'l', 'e', 'v', 'e', 'l',
        0x02, 0x00, 0x06, 's', 't', 'a', 't', 'u', 's',
        // "code" property
        0x00, 0x04, 'c', 'o', 'd', 'e',
        0x02, 0x00, 0x1d, 'N', 'e', 't', 'C', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'o', 'n', '.', 'C', 'o', 'n', 'n', 'e', 'c', 't', '.', 'S', 'u', 'c', 'c', 'e', 's', 's',
        // "description" property
        0x00, 0x0b, 'd', 'e', 's', 'c', 'r', 'i', 'p', 't', 'i', 'o', 'n',
        0x02, 0x00, 0x14, 'C', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'o', 'n', ' ', 's', 'u', 'c', 'c', 'e', 's', 's', 'e', 'd',
        // "objectEncoding" property
        0x00, 0x0e, 'o', 'b', 'j', 'e', 'c', 't', 'E', 'n', 'c', 'o', 'd', 'i', 'n', 'g',
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Number: 0
        // Object end
        0x00, 0x00, 0x09
    };
    
    send(session->socket, connectResult, sizeof(connectResult), 0);
    writeToLog(@"Enviado _result para connect");
}

- (void)sendCreateStreamResponse:(rtmp_session_t*)session {
    uint8_t createStreamResult[] = {
        // Header
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00, 0x00, 0x00, 0x00,
        // AMF string: "_result"
        0x02, 0x00, 0x07, '_', 'r', 'e', 's', 'u', 'l', 't',
        // Transaction ID (number) - deve ser o mesmo do request, mas usamos 2 fixo
        0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // NULL
        0x05,
        // Stream ID (1)
        0x00, 0x3f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    
    send(session->socket, createStreamResult, sizeof(createStreamResult), 0);
    writeToLog(@"Enviado _result para createStream com stream ID: 1");
}

- (void)sendPublishResponse:(rtmp_session_t*)session {
    // Resposta no formato do Node Media Server
    uint8_t publishStart[] = {
        // Header
        0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x01, 0x00, 0x00, 0x00,
        // AMF string: "onStatus"
        0x02, 0x00, 0x08, 'o', 'n', 'S', 't', 'a', 't', 'u', 's',
        // Transaction ID (0)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // NULL
        0x05,
        // Info object
        0x03,
        // "level" property
        0x00, 0x05, 'l', 'e', 'v', 'e', 'l',
        0x02, 0x00, 0x06, 's', 't', 'a', 't', 'u', 's',
        // "code" property
        0x00, 0x04, 'c', 'o', 'd', 'e',
        0x02, 0x00, 0x15, 'N', 'e', 't', 'S', 't', 'r', 'e', 'a', 'm', '.', 'P', 'u', 'b', 'l', 'i', 's', 'h', '.', 'S', 't', 'a', 'r', 't',
        // "description" property
        0x00, 0x0b, 'd', 'e', 's', 'c', 'r', 'i', 'p', 't', 'i', 'o', 'n',
        0x02, 0x00, 0x10, 'P', 'u', 'b', 'l', 'i', 's', 'h', ' ', 'S', 'u', 'c', 'c', 'e', 's', 's', '.',
        // Object end
        0x00, 0x00, 0x09
    };
    
    send(session->socket, publishStart, sizeof(publishStart), 0);
    writeToLog(@"Enviado onStatus - NetStream.Publish.Start");
    
    // Configurar para estado de streaming
    session->state = RTMP_STATE_STREAMING;
}

- (void)sendPlayResponse:(rtmp_session_t*)session {
    // Stream Begin first - needed by many players
    uint8_t streamBegin[] = {
        // Header
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00,
        // Stream Begin (UCM)
        0x00, 0x00, // Type
        0x00, 0x00, 0x00, 0x01  // Stream ID 1
    };
    send(session->socket, streamBegin, sizeof(streamBegin), 0);
    writeToLog(@"Enviado Stream Begin");
    
    // Stream Recorded flag
    uint8_t streamRecorded[] = {
        // Header
        0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x04, 0x00, 0x00, 0x00, 0x00,
        // Stream Recorded (UCM)
        0x00, 0x04, // Type
        0x00, 0x00, 0x00, 0x01  // Stream ID 1
    };
    send(session->socket, streamRecorded, sizeof(streamRecorded), 0);
    writeToLog(@"Enviado Stream Recorded");
    
    // Play Reset - clean buffer
    uint8_t playReset[] = {
        // Header
        0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x01, 0x00, 0x00, 0x00,
        // AMF string: "onStatus"
        0x02, 0x00, 0x08, 'o', 'n', 'S', 't', 'a', 't', 'u', 's',
        // Transaction ID (0)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // NULL
        0x05,
        // Info object
        0x03,
        // "level" property
        0x00, 0x05, 'l', 'e', 'v', 'e', 'l',
        0x02, 0x00, 0x06, 's', 't', 'a', 't', 'u', 's',
        // "code" property
        0x00, 0x04, 'c', 'o', 'd', 'e',
        0x02, 0x00, 0x12, 'N', 'e', 't', 'S', 't', 'r', 'e', 'a', 'm', '.', 'P', 'l', 'a', 'y', '.', 'R', 'e', 's', 'e', 't',
        // "description" property
        0x00, 0x0b, 'd', 'e', 's', 'c', 'r', 'i', 'p', 't', 'i', 'o', 'n',
        0x02, 0x00, 0x10, 'P', 'l', 'a', 'y', 'i', 'n', 'g', ' ', 'a', 'n', 'd', ' ', 'r', 'e', 's', 'e', 't',
        // Object end
        0x00, 0x00, 0x09
    };
    send(session->socket, playReset, sizeof(playReset), 0);
    writeToLog(@"Enviado Play Reset");
    
    // Play Start
    uint8_t playStart[] = {
        // Header
        0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x01, 0x00, 0x00, 0x00,
        // AMF string: "onStatus"
        0x02, 0x00, 0x08, 'o', 'n', 'S', 't', 'a', 't', 'u', 's',
        // Transaction ID (0)
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        // NULL
        0x05,
        // Info object
        0x03,
        // "level" property
        0x00, 0x05, 'l', 'e', 'v', 'e', 'l',
        0x02, 0x00, 0x06, 's', 't', 'a', 't', 'u', 's',
        // "code" property
        0x00, 0x04, 'c', 'o', 'd', 'e',
        0x02, 0x00, 0x14, 'N', 'e', 't', 'S', 't', 'r', 'e', 'a', 'm', '.', 'P', 'l', 'a', 'y', '.', 'S', 't', 'a', 'r', 't',
        // "description" property
        0x00, 0x0b, 'd', 'e', 's', 'c', 'r', 'i', 'p', 't', 'i', 'o', 'n',
        0x02, 0x00, 0x0e, 'S', 't', 'a', 'r', 't', ' ', 'l', 'i', 'v', 'e', ' ', 'p', 'l', 'a', 'y',
        // Object end
        0x00, 0x00, 0x09
    };
    
    send(session->socket, playStart, sizeof(playStart), 0);
    writeToLog(@"Enviado onStatus - NetStream.Play.Start");
    
    // Configurar para estado de streaming
    session->state = RTMP_STATE_STREAMING;
}

- (void)setupPreviewWindow {
    dispatch_async(dispatch_get_main_queue(), ^{
        CGRect previewFrame = CGRectMake(10, 40, 180, 280);
        self.statusView = [[RTMPStatusView alloc] initWithFrame:previewFrame];
        
        UIPanGestureRecognizer *panGesture = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(handlePan:)];
        [self.statusView addGestureRecognizer:panGesture];
        
        UIWindow *window = nil;
        NSArray *windows = [[UIApplication sharedApplication] windows];
        for (UIWindow *w in windows) {
            if (w.windowLevel == UIWindowLevelNormal) {
                window = w;
                break;
            }
        }
        
        if (!window) {
            writeToLog(@"Erro: Não foi possível encontrar uma janela válida");
            return;
        }
        
        [window addSubview:self.statusView];
        writeToLog(@"Interface de preview configurada");
        
        [self.statusView updateStatus:@"Inicializando servidor RTMP..."];
    });
}

- (void)updateStats {
    if (!self.isReceivingStream) {
        [self.statusView updateStats:@"Sem stream ativo"];
        return;
    }
    
    CFAbsoluteTime currentTime = CFAbsoluteTimeGetCurrent();
    CFAbsoluteTime elapsed = currentTime - _lastStatsUpdate;
    
    if (elapsed > 0) {
        self.currentFPS = _frameCount / elapsed;
        self.currentBitrate = (_byteCount * 8.0) / (elapsed * 1000000.0);
        
        NSString *stats = [NSString stringWithFormat:@"%.1f FPS | %.2f Mbps", self.currentFPS, self.currentBitrate];
        [self.statusView updateStats:stats];
        
        _frameCount = 0;
        _byteCount = 0;
        _lastStatsUpdate = currentTime;
    }
}

- (void)handlePan:(UIPanGestureRecognizer *)gesture {
    CGPoint translation = [gesture translationInView:gesture.view.superview];
    gesture.view.center = CGPointMake(gesture.view.center.x + translation.x,
                                    gesture.view.center.y + translation.y);
    [gesture setTranslation:CGPointZero inView:gesture.view.superview];
}

- (CMSampleBufferRef)createSampleBufferFromH264Data:(uint8_t *)data size:(uint32_t)size timestamp:(uint32_t)timestamp {
    if (!data || size == 0) {
        return NULL;
    }
    
    // Verificar tipo de pacote H.264 (importante para decodificação)
    uint8_t frameType = (data[0] & 0xF0) >> 4;
	// Use a variável frameType para log ou outras verificações
	writeToLog(@"Frame type: %d", frameType);
    uint8_t codecId = data[0] & 0x0F;
    
    if (codecId != 7) { // 7 = AVC/H.264
        writeToLog(@"Codec não suportado: %d", codecId);
        return NULL;
    }
    
    // Verificar pacote AVC
    uint8_t avcPacketType = data[1];
    // Pular primeiros 5 bytes (FLV tag + AVC NALU)
    uint8_t *h264Data = data + 5;
    uint32_t h264Size = size - 5;
    
    // Criar um formato de vídeo H.264
    CMVideoFormatDescriptionRef formatDescription = NULL;
    
    // Baseado no tipo de pacote AVC
    if (avcPacketType == 0) { // AVC sequence header
        writeToLog(@"Recebido AVC sequence header");
        
        // Tentar criar descrição a partir de parâmetros SPS/PPS
        const uint8_t *parameterSets[2] = {NULL, NULL};
        size_t parameterSetSizes[2] = {0, 0};
        
        // Encontrar SPS e PPS no AVC config record
        if (h264Size > 9) {
            uint16_t spsLength = (h264Data[6] << 8) | h264Data[7];
            if (8 + spsLength < h264Size) {
                parameterSets[0] = h264Data + 8;
                parameterSetSizes[0] = spsLength;
                
                uint8_t ppsCount = h264Data[8 + spsLength];
                if (ppsCount > 0 && 9 + spsLength < h264Size) {
                    uint16_t ppsLength = (h264Data[9 + spsLength] << 8) | h264Data[10 + spsLength];
                    if (11 + spsLength + ppsLength <= h264Size) {
                        parameterSets[1] = h264Data + 11 + spsLength;
                        parameterSetSizes[1] = ppsLength;
                    }
                }
            }
        }
        
        if (parameterSets[0] && parameterSetSizes[0] > 0) {
            OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
                kCFAllocatorDefault,
                parameterSets[1] ? 2 : 1,  // count
                parameterSets,
                parameterSetSizes,
                4,  // NAL length size
                &formatDescription
            );
            if (status != noErr) {
                writeToLog(@"Falha ao criar formatação H.264: %d", (int)status);
                return NULL;
            }
        } else {
            // Usar formatação genérica se não conseguimos extrair parâmetros
            OSStatus status = CMVideoFormatDescriptionCreate(
                kCFAllocatorDefault,
                kCMVideoCodecType_H264,
                1280, 720,  // dimensões estimadas
                NULL,
                &formatDescription
            );
            if (status != noErr) {
                return NULL;
            }
        }
    } else if (avcPacketType == 1) { // AVC NALU
        // Para NALUs, usamos formatação simples
        OSStatus status = CMVideoFormatDescriptionCreate(
            kCFAllocatorDefault,
            kCMVideoCodecType_H264,
            1280, 720,
            NULL,
            &formatDescription
        );
        if (status != noErr) {
            return NULL;
        }
    } else {
        // Tipo não suportado
        return NULL;
    }
	// Criar timing info
    CMSampleTimingInfo timing = {
        .duration = CMTimeMake(1, 30),
        .presentationTimeStamp = CMTimeMake(timestamp, 1000),
        .decodeTimeStamp = CMTimeMake(timestamp, 1000)
    };

    // Criar buffer de dados
    CMBlockBufferRef blockBuffer;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock(
        kCFAllocatorDefault,
        h264Data,
        h264Size,
        kCFAllocatorNull,
        NULL,
        0,
        h264Size,
        0,
        &blockBuffer
    );
    
    if (status != noErr) {
        CFRelease(formatDescription);
        return NULL;
    }

    // Criar sample buffer
    CMSampleBufferRef sampleBuffer;
    status = CMSampleBufferCreate(
        kCFAllocatorDefault,
        blockBuffer,
        true,
        NULL,
        NULL,
        formatDescription,
        1,
        1,
        &timing,
        0,
        NULL,
        &sampleBuffer
    );

    CFRelease(blockBuffer);
    CFRelease(formatDescription);

    return status == noErr ? sampleBuffer : NULL;
}

@end

%hook SpringBoard
- (void)applicationDidFinishLaunching:(id)application {
    %orig;
    writeToLog(@"RTMPCameraTweak inicializando...");
    
    RTMPCameraManager *manager = [RTMPCameraManager sharedInstance];
    [manager setupPreviewWindow];
    [manager startRTMPServer];
}
%end

%hook AVCaptureVideoDataOutput
- (void)captureOutput:(AVCaptureOutput *)output didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer fromConnection:(AVCaptureConnection *)connection {
    RTMPCameraManager *manager = [RTMPCameraManager sharedInstance];
    
    if (manager.isReceivingStream && manager.currentBuffer) {
        %orig(output, manager.currentBuffer, connection);
    } else {
        %orig;
    }
}
%end

%ctor {
    writeToLog(@"RTMPCameraTweak carregado");
}