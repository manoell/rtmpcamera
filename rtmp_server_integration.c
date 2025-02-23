#include "rtmp_server_integration.h"
#include "rtmp_utils.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_PENDING_CONNECTIONS 10
#define MAX_CLIENTS 100
#define BUFFER_SIZE 8192
#define CLIENT_TIMEOUT 30000  // 30 seconds
#define HEALTH_CHECK_INTERVAL 1000 // 1 second
#define DEFAULT_CHUNK_SIZE 128
#define DEFAULT_WINDOW_SIZE 2500000

// Client structure
typedef struct {
    int socket;
    char id[64];
    char ip[32];
    uint32_t connectTime;
    uint32_t lastActivity;
    uint32_t bytesSent;
    uint32_t bytesReceived;
    bool authenticated;
    bool isPublisher;
    RTMPContext *rtmp;
    char *streamName;
    uint32_t streamId;
    pthread_t thread;
    bool threadRunning;
} RTMPServerClient;

// Stream structure
typedef struct RTMPServerStream {
    char name[128];
    RTMPServerClient *publisher;
    RTMPServerClient **subscribers;
    uint32_t numSubscribers;
    uint32_t maxSubscribers;
    uint32_t startTime;
    uint32_t bandwidth;
    RTMPStreamInfo info;
    struct RTMPServerStream *next;
} RTMPServerStream;

struct RTMPServerContext {
    RTMPServerConfig config;
    RTMPServerStats stats;
    RTMPServerState state;
    int serverSocket;
    RTMPServerClient *clients;
    uint32_t numClients;
    RTMPServerStream *streams;
    pthread_t acceptThread;
    pthread_t monitorThread;
    pthread_mutex_t mutex;
    pthread_mutex_t streamMutex;
    bool threadsRunning;
    RTMPServerClientCallback clientCallback;
    RTMPServerStreamCallback streamCallback;
    RTMPServerErrorCallback errorCallback;
    void *clientCallbackData;
    void *streamCallbackData;
    void *errorCallbackData;
    uint32_t startTime;
};

// Forward declarations
static void *accept_thread(void *arg);
static void *monitor_thread(void *arg);
static void *client_thread(void *arg);

// Private helper functions
static bool init_server_socket(RTMPServerContext *ctx);
static bool handle_client_connection(RTMPServerContext *ctx, int clientSocket, const char *clientIP);
static void handle_client_disconnect(RTMPServerContext *ctx, RTMPServerClient *client);
static bool authenticate_client(RTMPServerContext *ctx, RTMPServerClient *client);
static void check_client_timeouts(RTMPServerContext *ctx);
static void update_stats(RTMPServerContext *ctx);
static bool is_ip_allowed(RTMPServerContext *ctx, const char *ip);
static void log_error(RTMPServerContext *ctx, RTMPError error, const char *description);

// Stream management helpers
static RTMPServerStream *create_stream(const char *name, RTMPServerClient *publisher);
static void destroy_stream(RTMPServerStream *stream);
static RTMPServerStream *find_stream(RTMPServerContext *ctx, const char *name);
static bool add_subscriber_to_stream(RTMPServerStream *stream, RTMPServerClient *client);
static void remove_subscriber_from_stream(RTMPServerStream *stream, RTMPServerClient *client);
static void broadcast_to_subscribers(RTMPServerStream *stream, RTMPPacket *packet);

// Client management helpers
static RTMPServerClient *create_client(int socket, const char *ip);
static void destroy_client(RTMPServerClient *client);
static bool handle_client_message(RTMPServerClient *client, RTMPPacket *packet);
static bool send_server_bw(RTMPServerClient *client);
static bool send_client_bw(RTMPServerClient *client);
static bool send_chunk_size(RTMPServerClient *client);
static bool handle_connect(RTMPServerClient *client, RTMPPacket *packet);
static bool handle_createStream(RTMPServerClient *client, RTMPPacket *packet);
static bool handle_publish(RTMPServerClient *client, RTMPPacket *packet);
static bool handle_play(RTMPServerClient *client, RTMPPacket *packet);
static bool handle_closeStream(RTMPServerClient *client, RTMPPacket *packet);

// Buffer management
static bool write_client_buffer(RTMPServerClient *client, const uint8_t *data, size_t size);
static bool read_client_buffer(RTMPServerClient *client, uint8_t *data, size_t size);
static void flush_client_buffer(RTMPServerClient *client);

RTMPServerContext *rtmp_server_create(void) {
    RTMPServerContext *ctx = (RTMPServerContext *)calloc(1, sizeof(RTMPServerContext));
    if (!ctx) return NULL;

    // Initialize mutexes
    pthread_mutex_init(&ctx->mutex, NULL);
    pthread_mutex_init(&ctx->streamMutex, NULL);

    // Allocate client array
    ctx->clients = (RTMPServerClient *)calloc(MAX_CLIENTS, sizeof(RTMPServerClient));
    if (!ctx->clients) {
        free(ctx);
        return NULL;
    }

    // Set default config
    ctx->config.port = 1935;
    ctx->config.maxClients = MAX_CLIENTS;
    ctx->config.maxBandwidthPerClient = 5000000; // 5 Mbps
    ctx->config.chunkSize = DEFAULT_CHUNK_SIZE;
    ctx->config.windowSize = DEFAULT_WINDOW_SIZE;
    ctx->config.enableAuth = false;
    ctx->config.numAllowedIPs = 0;

    ctx->state = RTMP_SERVER_STATE_STOPPED;
    ctx->streams = NULL;
    ctx->startTime = rtmp_get_timestamp();

    return ctx;
}

void rtmp_server_destroy(RTMPServerContext *ctx) {
    if (!ctx) return;

    rtmp_server_stop(ctx);

    // Free all streams
    pthread_mutex_lock(&ctx->streamMutex);
    RTMPServerStream *stream = ctx->streams;
    while (stream) {
        RTMPServerStream *next = stream->next;
        destroy_stream(stream);
        stream = next;
    }
    pthread_mutex_unlock(&ctx->streamMutex);

    // Free clients array
    free(ctx->clients);

    // Destroy mutexes
    pthread_mutex_destroy(&ctx->mutex);
    pthread_mutex_destroy(&ctx->streamMutex);

    free(ctx);
}

bool rtmp_server_start(RTMPServerContext *ctx) {
    if (!ctx || ctx->state != RTMP_SERVER_STATE_STOPPED) return false;

    pthread_mutex_lock(&ctx->mutex);

    // Initialize server socket
    if (!init_server_socket(ctx)) {
        pthread_mutex_unlock(&ctx->mutex);
        return false;
    }

    // Reset statistics
    memset(&ctx->stats, 0, sizeof(RTMPServerStats));
    ctx->stats.uptime = 0;
    ctx->startTime = rtmp_get_timestamp();

    // Start threads
    ctx->threadsRunning = true;
    ctx->state = RTMP_SERVER_STATE_STARTING;

    if (pthread_create(&ctx->acceptThread, NULL, accept_thread, ctx) != 0) {
        log_error(ctx, RTMP_ERROR_THREAD_CREATE_FAILED, "Failed to create accept thread");
        close(ctx->serverSocket);
        ctx->state = RTMP_SERVER_STATE_STOPPED;
        pthread_mutex_unlock(&ctx->mutex);
        return false;
    }

    if (pthread_create(&ctx->monitorThread, NULL, monitor_thread, ctx) != 0) {
        log_error(ctx, RTMP_ERROR_THREAD_CREATE_FAILED, "Failed to create monitor thread");
        ctx->threadsRunning = false;
        pthread_join(ctx->acceptThread, NULL);
        close(ctx->serverSocket);
        ctx->state = RTMP_SERVER_STATE_STOPPED;
        pthread_mutex_unlock(&ctx->mutex);
        return false;
    }

    ctx->state = RTMP_SERVER_STATE_RUNNING;
    pthread_mutex_unlock(&ctx->mutex);
    return true;
}

void rtmp_server_stop(RTMPServerContext *ctx) {
    if (!ctx || ctx->state == RTMP_SERVER_STATE_STOPPED) return;

    pthread_mutex_lock(&ctx->mutex);

    // Stop threads
    ctx->threadsRunning = false;
    pthread_join(ctx->acceptThread, NULL);
    pthread_join(ctx->monitorThread, NULL);

    // Disconnect all clients
    for (uint32_t i = 0; i < ctx->numClients; i++) {
        handle_client_disconnect(ctx, &ctx->clients[i]);
    }

    // Close server socket
    if (ctx->serverSocket >= 0) {
        close(ctx->serverSocket);
        ctx->serverSocket = -1;
    }

    ctx->state = RTMP_SERVER_STATE_STOPPED;
    ctx->numClients = 0;

    pthread_mutex_unlock(&ctx->mutex);
}

static bool init_server_socket(RTMPServerContext *ctx) {
    // Create server socket
    ctx->serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (ctx->serverSocket < 0) {
        log_error(ctx, RTMP_ERROR_SOCKET_CREATE_FAILED, "Failed to create server socket");
        return false;
    }

    // Set socket options
    int opt = 1;
    setsockopt(ctx->serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // Set non-blocking
    int flags = fcntl(ctx->serverSocket, F_GETFL, 0);
    fcntl(ctx->serverSocket, F_SETFL, flags | O_NONBLOCK);

    // Bind socket
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(ctx->config.port);

    if (bind(ctx->serverSocket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error(ctx, RTMP_ERROR_SOCKET_BIND_FAILED, "Failed to bind server socket");
        close(ctx->serverSocket);
        return false;
    }

    // Listen for connections
    if (listen(ctx->serverSocket, MAX_PENDING_CONNECTIONS) < 0) {
        log_error(ctx, RTMP_ERROR_SOCKET_LISTEN_FAILED, "Failed to listen on server socket");
        close(ctx->serverSocket);
        return false;
    }

    return true;
}

static RTMPServerClient *create_client(int socket, const char *ip) {
    RTMPServerClient *client = (RTMPServerClient *)calloc(1, sizeof(RTMPServerClient));
    if (!client) return NULL;

    client->socket = socket;
    strncpy(client->ip, ip, sizeof(client->ip) - 1);
    client->connectTime = rtmp_get_timestamp();
    client->lastActivity = client->connectTime;
    client->threadRunning = true;

    // Set socket to non-blocking
    int flags = fcntl(socket, F_GETFL, 0);
    fcntl(socket, F_SETFL, flags | O_NONBLOCK);

    // Create RTMP context
    client->rtmp = rtmp_create();
    if (!client->rtmp) {
        free(client);
        return NULL;
    }

    return client;
}

static void destroy_client(RTMPServerClient *client) {
    if (!client) return;

    // Stop client thread
    if (client->threadRunning) {
        client->threadRunning = false;
        pthread_join(client->thread, NULL);
    }

    // Close socket
    if (client->socket >= 0) {
        close(client->socket);
        client->socket = -1;
    }

    // Free RTMP context
    if (client->rtmp) {
        rtmp_destroy(client->rtmp);
        client->rtmp = NULL;
    }

    // Free stream name
    if (client->streamName) {
        free(client->streamName);
        client->streamName = NULL;
    }

    memset(client, 0, sizeof(RTMPServerClient));
}

static RTMPServerStream *create_stream(const char *name, RTMPServerClient *publisher) {
    RTMPServerStream *stream = (RTMPServerStream *)calloc(1, sizeof(RTMPServerStream));
    if (!stream) return NULL;

    strncpy(stream->name, name, sizeof(stream->name) - 1);
    stream->publisher = publisher;
    stream->startTime = rtmp_get_timestamp();
    stream->maxSubscribers = 100;

    stream->subscribers = (RTMPServerClient **)calloc(stream->maxSubscribers, 
                                                    sizeof(RTMPServerClient *));
    if (!stream->subscribers) {
        free(stream);
        return NULL;
    }

    return stream;
}

static void destroy_stream(RTMPServerStream *stream) {
    if (!stream) return;

    // Notify all subscribers
    for (uint32_t i = 0; i < stream->numSubscribers; i++) {
        if (stream->subscribers[i]) {
            RTMPPacket packet = {
                .type = RTMP_MSG_USER_CONTROL,
                .data = (uint8_t *)"\x02\x00\x00\x00\x00\x00", // Stream EOF
                .size = 6
            };
            rtmp_send_packet(stream->subscribers[i]->rtmp, &packet);
        }
    }

    free(stream->subscribers);
    free(stream);
}

static void *accept_thread(void *arg) {
    RTMPServerContext *ctx = (RTMPServerContext *)arg;
    struct sockaddr_in clientAddr;
    socklen_t addrLen = sizeof(clientAddr);

    while (ctx->threadsRunning) {
        // Accept new connection
        int clientSocket = accept(ctx->serverSocket, 
                                (struct sockaddr *)&clientAddr, 
                                &addrLen);
                                
        if (clientSocket < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK && ctx->threadsRunning) {
                log_error(ctx, RTMP_ERROR_SOCKET_ACCEPT_FAILED, "Failed to accept client connection");
            }
            rtmp_sleep_ms(10);
            continue;
        }

        // Get client IP
        char clientIP[32];
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, sizeof(clientIP));

        pthread_mutex_lock(&ctx->mutex);

        // Check if IP is allowed
        if (ctx->config.numAllowedIPs > 0 && !is_ip_allowed(ctx, clientIP)) {
            close(clientSocket);
            ctx->stats.droppedConnections++;
            pthread_mutex_unlock(&ctx->mutex);
            continue;
        }

        // Check max clients
        if (ctx->numClients >= ctx->config.maxClients) {
            close(clientSocket);
            ctx->stats.droppedConnections++;
            pthread_mutex_unlock(&ctx->mutex);
            continue;
        }

        // Create and initialize client
        RTMPServerClient *client = create_client(clientSocket, clientIP);
        if (!client) {
            close(clientSocket);
            ctx->stats.droppedConnections++;
            pthread_mutex_unlock(&ctx->mutex);
            continue;
        }

        // Add client to array
        ctx->clients[ctx->numClients] = *client;
        free(client);

        // Start client thread
        if (pthread_create(&ctx->clients[ctx->numClients].thread, NULL, 
                          client_thread, &ctx->clients[ctx->numClients]) != 0) {
            destroy_client(&ctx->clients[ctx->numClients]);
            ctx->stats.droppedConnections++;
            pthread_mutex_unlock(&ctx->mutex);
            continue;
        }

        ctx->numClients++;
        ctx->stats.totalClients++;

        // Notify callback
        if (ctx->clientCallback) {
            ctx->clientCallback(ctx, ctx->clients[ctx->numClients-1].id, 
                              true, ctx->clientCallbackData);
        }

        pthread_mutex_unlock(&ctx->mutex);
    }

    return NULL;
}

static void *monitor_thread(void *arg) {
    RTMPServerContext *ctx = (RTMPServerContext *)arg;
    uint32_t lastCheck = rtmp_get_timestamp();

    while (ctx->threadsRunning) {
        pthread_mutex_lock(&ctx->mutex);

        uint32_t now = rtmp_get_timestamp();
        if (now - lastCheck >= HEALTH_CHECK_INTERVAL) {
            check_client_timeouts(ctx);
            update_stats(ctx);
            lastCheck = now;
        }

        pthread_mutex_unlock(&ctx->mutex);
        rtmp_sleep_ms(100);
    }

    return NULL;
}

static void *client_thread(void *arg) {
    RTMPServerClient *client = (RTMPServerClient *)arg;
    uint8_t buffer[BUFFER_SIZE];
    RTMPPacket packet;

    // Handshake
    if (!rtmp_handshake(client->rtmp)) {
        client->threadRunning = false;
        return NULL;
    }

    while (client->threadRunning) {
        // Read RTMP packet
        if (!rtmp_read_packet(client->rtmp, &packet)) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                break;
            }
            rtmp_sleep_ms(1);
            continue;
        }

        client->lastActivity = rtmp_get_timestamp();
        client->bytesReceived += packet.size;

        // Handle packet
        if (!handle_client_message(client, &packet)) {
            break;
        }

        // Free packet data if allocated
        if (packet.data) {
            free(packet.data);
        }
    }

    client->threadRunning = false;
    return NULL;
}

static bool handle_client_message(RTMPServerClient *client, RTMPPacket *packet) {
    if (!client || !packet) return false;

    switch (packet->type) {
        case RTMP_MSG_CHUNK_SIZE:
            return rtmp_handle_chunk_size(client->rtmp, packet);

        case RTMP_MSG_BYTES_READ:
            return rtmp_handle_bytes_read(client->rtmp, packet);

        case RTMP_MSG_CONTROL:
            return rtmp_handle_control(client->rtmp, packet);

        case RTMP_MSG_SERVER_BW:
            return rtmp_handle_server_bw(client->rtmp, packet);

        case RTMP_MSG_CLIENT_BW:
            return rtmp_handle_client_bw(client->rtmp, packet);

        case RTMP_MSG_AUDIO:
        case RTMP_MSG_VIDEO:
            if (client->isPublisher) {
                return handle_media_packet(client, packet);
            }
            break;

        case RTMP_MSG_AMF3_COMMAND:
        case RTMP_MSG_AMF0_COMMAND:
            return handle_command_packet(client, packet);

        case RTMP_MSG_METADATA:
            return handle_metadata_packet(client, packet);

        default:
            rtmp_log(RTMP_LOG_WARNING, "Unhandled message type: %d", packet->type);
            break;
    }

    return true;
}

static bool handle_command_packet(RTMPServerClient *client, RTMPPacket *packet) {
    char *command = NULL;
    if (!rtmp_amf_decode_string(packet->data, packet->size, &command)) {
        return false;
    }

    bool result = false;

    if (strcmp(command, "connect") == 0) {
        result = handle_connect(client, packet);
    }
    else if (strcmp(command, "createStream") == 0) {
        result = handle_createStream(client, packet);
    }
    else if (strcmp(command, "publish") == 0) {
        result = handle_publish(client, packet);
    }
    else if (strcmp(command, "play") == 0) {
        result = handle_play(client, packet);
    }
    else if (strcmp(command, "closeStream") == 0) {
        result = handle_closeStream(client, packet);
    }
    else if (strcmp(command, "deleteStream") == 0) {
        result = handle_closeStream(client, packet);
    }
    else {
        rtmp_log(RTMP_LOG_WARNING, "Unhandled command: %s", command);
        result = true; // Don't disconnect for unknown commands
    }

    free(command);
    return result;
}

static bool handle_connect(RTMPServerClient *client, RTMPPacket *packet) {
    // Extract connect parameters
    AMFObject *obj = NULL;
    if (!rtmp_amf_decode_object(packet->data + packet->size - AMF_MAX_SIZE, &obj)) {
        return false;
    }

    // Check authentication if enabled
    if (client->rtmp->server->config.enableAuth) {
        const char *token = amf_object_get_string(obj, "auth");
        if (!token || strcmp(token, client->rtmp->server->config.authKey) != 0) {
            amf_object_destroy(obj);
            send_connect_error(client, "Authentication failed");
            client->rtmp->server->stats.failedAuths++;
            return false;
        }
    }

    // Save app name
    const char *app = amf_object_get_string(obj, "app");
    if (app) {
        strncpy(client->rtmp->app, app, sizeof(client->rtmp->app) - 1);
    }

    amf_object_destroy(obj);

    // Send response
    RTMPPacket response;
    memset(&response, 0, sizeof(response));
    response.type = RTMP_MSG_AMF0_COMMAND;
    
    char *resp = malloc(256);
    size_t len = 0;
    
    // _result
    len += rtmp_amf_encode_string(resp + len, "_result");
    // transaction ID
    len += rtmp_amf_encode_number(resp + len, 1.0);
    // Properties object
    len += rtmp_amf_encode_object_start(resp + len);
    len += rtmp_amf_encode_named_string(resp + len, "fmsVer", "FMS/3,0,1,123");
    len += rtmp_amf_encode_named_number(resp + len, "capabilities", 31.0);
    len += rtmp_amf_encode_object_end(resp + len);
    // Information object
    len += rtmp_amf_encode_object_start(resp + len);
    len += rtmp_amf_encode_named_string(resp + len, "level", "status");
    len += rtmp_amf_encode_named_string(resp + len, "code", "NetConnection.Connect.Success");
    len += rtmp_amf_encode_named_string(resp + len, "description", "Connection succeeded.");
    len += rtmp_amf_encode_object_end(resp + len);

    response.data = (uint8_t *)resp;
    response.size = len;

    bool result = rtmp_send_packet(client->rtmp, &response);
    free(resp);

    if (result) {
        // Send window acknowledgement size
        send_server_bw(client);
        send_client_bw(client);
        send_chunk_size(client);
    }

    return result;
}

static bool handle_createStream(RTMPServerClient *client, RTMPPacket *packet) {
    // Extract transaction ID
    double transactionId;
    if (!rtmp_amf_decode_number(packet->data + AMF_STRING_SIZE, &transactionId)) {
        return false;
    }

    // Create stream ID
    uint32_t streamId = ++client->rtmp->server->stats.totalStreams;
    client->streamId = streamId;

    // Send response
    RTMPPacket response;
    memset(&response, 0, sizeof(response));
    response.type = RTMP_MSG_AMF0_COMMAND;
    
    char *resp = malloc(256);
    size_t len = 0;
    
    // _result
    len += rtmp_amf_encode_string(resp + len, "_result");
    // transaction ID
    len += rtmp_amf_encode_number(resp + len, transactionId);
    // NULL object
    len += rtmp_amf_encode_null(resp + len);
    // Stream ID
    len += rtmp_amf_encode_number(resp + len, streamId);

    response.data = (uint8_t *)resp;
    response.size = len;

    bool result = rtmp_send_packet(client->rtmp, &response);
    free(resp);

    return result;
}

static bool handle_publish(RTMPServerClient *client, RTMPPacket *packet) {
    // Extract stream name and type
    char *streamName = NULL;
    char *publishType = NULL;
    
    size_t offset = AMF_STRING_SIZE + sizeof(double) + AMF_NULL_SIZE;
    if (!rtmp_amf_decode_string(packet->data + offset, &streamName)) {
        return false;
    }
    
    offset += strlen(streamName) + 1;
    if (!rtmp_amf_decode_string(packet->data + offset, &publishType)) {
        free(streamName);
        return false;
    }

    bool result = false;
    pthread_mutex_lock(&client->rtmp->server->streamMutex);

    // Check if stream already exists
    RTMPServerStream *existingStream = find_stream(client->rtmp->server, streamName);
    if (existingStream) {
        send_error(client, "NetStream.Publish.BadName", "Stream already exists");
    } else {
        // Create new stream
        RTMPServerStream *stream = create_stream(streamName, client);
        if (stream) {
            client->isPublisher = true;
            client->streamName = strdup(streamName);
            
            // Add to stream list
            stream->next = client->rtmp->server->streams;
            client->rtmp->server->streams = stream;

            // Send success response
            send_status(client, "NetStream.Publish.Start", "Stream publishing started");
            result = true;

            // Notify callback
            if (client->rtmp->server->streamCallback) {
                client->rtmp->server->streamCallback(client->rtmp->server, 
                                                   streamName, 
                                                   true, 
                                                   client->rtmp->server->streamCallbackData);
            }
        }
    }

    pthread_mutex_unlock(&client->rtmp->server->streamMutex);

    free(streamName);
    free(publishType);

    return result;
}

static bool handle_play(RTMPServerClient *client, RTMPPacket *packet) {
    // Extract stream name
    char *streamName = NULL;
    if (!rtmp_amf_decode_string(packet->data + AMF_STRING_SIZE + sizeof(double) + AMF_NULL_SIZE, 
                               &streamName)) {
        return false;
    }

    bool result = false;
    pthread_mutex_lock(&client->rtmp->server->streamMutex);

    // Find stream
    RTMPServerStream *stream = find_stream(client->rtmp->server, streamName);
    if (!stream) {
        send_error(client, "NetStream.Play.StreamNotFound", "Stream not found");
    } else {
        // Add as subscriber
        if (add_subscriber_to_stream(stream, client)) {
            client->streamName = strdup(streamName);
            
            // Send success response
            send_status(client, "NetStream.Play.Start", "Stream playback started");
            
            // Send stream begin signal
            RTMPPacket beginPacket = {
                .type = RTMP_MSG_USER_CONTROL,
                .data = (uint8_t *)"\x00\x00\x00\x00\x00\x00", // Stream Begin
                .size = 6
            };
            *(uint32_t *)(beginPacket.data + 2) = htonl(client->streamId);
            
            result = rtmp_send_packet(client->rtmp, &beginPacket);

            // Send metadata and keyframe
            if (stream->publisher) {
                // Send cached metadata if available
                if (stream->info.metadata) {
                    RTMPPacket metaPacket = {
                        .type = RTMP_MSG_METADATA,
                        .data = stream->info.metadata,
                        .size = stream->info.metadataSize
                    };
                    rtmp_send_packet(client->rtmp, &metaPacket);
                }

                // Send cached keyframe if available
                if (stream->info.videoKeyframe) {
                    RTMPPacket keyframePacket = {
                        .type = RTMP_MSG_VIDEO,
                        .data = stream->info.videoKeyframe,
                        .size = stream->info.keyframeSize
                    };
                    rtmp_send_packet(client->rtmp, &keyframePacket);
                }
            }
        }
    }

    pthread_mutex_unlock(&client->rtmp->server->streamMutex);
    free(streamName);

    return result;
}

static bool handle_media_packet(RTMPServerClient *client, RTMPPacket *packet) {
    if (!client->isPublisher) return false;

    pthread_mutex_lock(&client->rtmp->server->streamMutex);

    RTMPServerStream *stream = find_stream(client->rtmp->server, client->streamName);
    if (stream) {
        // If video packet, check for keyframe
        if (packet->type == RTMP_MSG_VIDEO && 
            packet->data && 
            (packet->data[0] & 0xf0) == 0x10) { // Keyframe
            
            // Cache keyframe
            if (stream->info.videoKeyframe) {
                free(stream->info.videoKeyframe);
            }
            stream->info.videoKeyframe = malloc(packet->size);
            if (stream->info.videoKeyframe) {
                memcpy(stream->info.videoKeyframe, packet->data, packet->size);
                stream->info.keyframeSize = packet->size;
            }
        }

        // Broadcast to all subscribers
        broadcast_to_subscribers(stream, packet);

        // Update stream statistics
        stream->bandwidth += packet->size;
        
        // Update client statistics
        client->bytesSent += packet->size;
    }

    pthread_mutex_unlock(&client->rtmp->server->streamMutex);
    return true;
}

static bool handle_metadata_packet(RTMPServerClient *client, RTMPPacket *packet) {
    if (!client->isPublisher) return false;

    pthread_mutex_lock(&client->rtmp->server->streamMutex);

    RTMPServerStream *stream = find_stream(client->rtmp->server, client->streamName);
    if (stream) {
        // Cache metadata
        if (stream->info.metadata) {
            free(stream->info.metadata);
        }
        stream->info.metadata = malloc(packet->size);
        if (stream->info.metadata) {
            memcpy(stream->info.metadata, packet->data, packet->size);
            stream->info.metadataSize = packet->size;
        }

        // Parse metadata
        AMFObject *obj = NULL;
        if (rtmp_amf_decode_object(packet->data, &obj)) {
            // Extract video dimensions
            double width = 0, height = 0;
            amf_object_get_number(obj, "width", &width);
            amf_object_get_number(obj, "height", &height);
            stream->info.width = (uint32_t)width;
            stream->info.height = (uint32_t)height;

            // Extract framerate
            double frameRate = 0;
            amf_object_get_number(obj, "framerate", &frameRate);
            stream->info.frameRate = (uint32_t)frameRate;

            // Extract video codec
            const char *videoCodec = amf_object_get_string(obj, "videocodecid");
            if (videoCodec) {
                strncpy(stream->info.videoCodec, videoCodec, sizeof(stream->info.videoCodec) - 1);
            }

            // Extract audio codec
            const char *audioCodec = amf_object_get_string(obj, "audiocodecid");
            if (audioCodec) {
                strncpy(stream->info.audioCodec, audioCodec, sizeof(stream->info.audioCodec) - 1);
            }

            amf_object_destroy(obj);
        }

        // Broadcast metadata to subscribers
        broadcast_to_subscribers(stream, packet);
    }

    pthread_mutex_unlock(&client->rtmp->server->streamMutex);
    return true;
}

static void broadcast_to_subscribers(RTMPServerStream *stream, RTMPPacket *packet) {
    for (uint32_t i = 0; i < stream->numSubscribers; i++) {
        RTMPServerClient *subscriber = stream->subscribers[i];
        if (subscriber && subscriber->rtmp) {
            RTMPPacket copy = *packet;
            copy.data = malloc(packet->size);
            if (copy.data) {
                memcpy(copy.data, packet->data, packet->size);
                if (rtmp_send_packet(subscriber->rtmp, &copy)) {
                    subscriber->bytesSent += packet->size;
                }
                free(copy.data);
            }
        }
    }
}

static void send_error(RTMPServerClient *client, const char *code, const char *description) {
    RTMPPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = RTMP_MSG_AMF0_COMMAND;

    char *data = malloc(1024);
    size_t len = 0;

    // onStatus
    len += rtmp_amf_encode_string(data + len, "onStatus");
    // transaction ID = 0
    len += rtmp_amf_encode_number(data + len, 0);
    // NULL object
    len += rtmp_amf_encode_null(data + len);
    // Info object
    len += rtmp_amf_encode_object_start(data + len);
    len += rtmp_amf_encode_named_string(data + len, "level", "error");
    len += rtmp_amf_encode_named_string(data + len, "code", code);
    len += rtmp_amf_encode_named_string(data + len, "description", description);
    len += rtmp_amf_encode_object_end(data + len);

    packet.data = (uint8_t *)data;
    packet.size = len;
    packet.streamId = client->streamId;

    rtmp_send_packet(client->rtmp, &packet);
    free(data);
}

static void send_status(RTMPServerClient *client, const char *code, const char *description) {
    RTMPPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = RTMP_MSG_AMF0_COMMAND;

    char *data = malloc(1024);
    size_t len = 0;

    // onStatus
    len += rtmp_amf_encode_string(data + len, "onStatus");
    // transaction ID = 0
    len += rtmp_amf_encode_number(data + len, 0);
    // NULL object
    len += rtmp_amf_encode_null(data + len);
    // Info object
    len += rtmp_amf_encode_object_start(data + len);
    len += rtmp_amf_encode_named_string(data + len, "level", "status");
    len += rtmp_amf_encode_named_string(data + len, "code", code);
    len += rtmp_amf_encode_named_string(data + len, "description", description);
    len += rtmp_amf_encode_object_end(data + len);

    packet.data = (uint8_t *)data;
    packet.size = len;
    packet.streamId = client->streamId;

    rtmp_send_packet(client->rtmp, &packet);
    free(data);
}

static bool send_server_bw(RTMPServerClient *client) {
    RTMPPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = RTMP_MSG_SERVER_BW;
    
    uint8_t *data = malloc(4);
    *(uint32_t *)data = htonl(client->rtmp->server->config.maxBandwidthPerClient);
    
    packet.data = data;
    packet.size = 4;
    
    bool result = rtmp_send_packet(client->rtmp, &packet);
    free(data);
    return result;
}

static bool send_client_bw(RTMPServerClient *client) {
    RTMPPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = RTMP_MSG_CLIENT_BW;
    
    uint8_t *data = malloc(5);
    *(uint32_t *)data = htonl(client->rtmp->server->config.maxBandwidthPerClient);
    data[4] = 2; // Dynamic
    
    packet.data = data;
    packet.size = 5;
    
    bool result = rtmp_send_packet(client->rtmp, &packet);
    free(data);
    return result;
}

static bool send_chunk_size(RTMPServerClient *client) {
    RTMPPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.type = RTMP_MSG_CHUNK_SIZE;
    
    uint8_t *data = malloc(4);
    *(uint32_t *)data = htonl(client->rtmp->server->config.chunkSize);
    
    packet.data = data;
    packet.size = 4;
    
    bool result = rtmp_send_packet(client->rtmp, &packet);
    free(data);
    return result;
}

static RTMPServerStream *find_stream(RTMPServerContext *ctx, const char *name) {
    RTMPServerStream *stream = ctx->streams;
    while (stream) {
        if (strcmp(stream->name, name) == 0) {
            return stream;
        }
        stream = stream->next;
    }
    return NULL;
}

static bool add_subscriber_to_stream(RTMPServerStream *stream, RTMPServerClient *client) {
    if (stream->numSubscribers >= stream->maxSubscribers) {
        return false;
    }
    
    stream->subscribers[stream->numSubscribers++] = client;
    return true;
}

static void check_client_timeouts(RTMPServerContext *ctx) {
    uint32_t now = rtmp_get_timestamp();
    
    for (uint32_t i = 0; i < ctx->numClients; i++) {
        RTMPServerClient *client = &ctx->clients[i];
        if (now - client->lastActivity >= CLIENT_TIMEOUT) {
            handle_client_disconnect(ctx, client);
            i--; // Adjust index since array was modified
        }
    }
}

static void update_stats(RTMPServerContext *ctx) {
    uint32_t now = rtmp_get_timestamp();
    ctx->stats.uptime = (now - ctx->startTime) / 1000; // Convert to seconds
}

static void log_error(RTMPServerContext *ctx, RTMPError error, const char *description) {
    rtmp_log(RTMP_LOG_ERROR, "Server error: %s", description);
    
    if (ctx->errorCallback) {
        ctx->errorCallback(ctx, error, description, ctx->errorCallbackData);
    }
}