// rtmp_server_integration.c
#include "rtmp_server_integration.h"
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>

// Private variables
static rtmp_server_context_t server_ctx;
static rtmp_connection_callback_t connection_callback;
static rtmp_metadata_callback_t metadata_callback;
static rtmp_frame_callback_t frame_callback;
static rtmp_server_state_callback_t state_callback;
static void* connection_callback_data;
static void* metadata_callback_data;
static void* frame_callback_data;
static void* state_callback_data;

// Forward declarations of internal functions
static void* rtmp_server_accept_thread(void* arg);
static void* rtmp_server_monitor_thread(void* arg);
static bool rtmp_server_handle_connection(rtmp_connection_t* conn);
static void rtmp_server_cleanup_connection(rtmp_connection_t* conn);
static void rtmp_server_update_state(rtmp_server_state_t new_state);
static bool rtmp_connection_send_chunk(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk);
static bool rtmp_connection_receive_chunk(rtmp_connection_t* conn);
static void rtmp_connection_handle_message(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk);
static bool rtmp_handshake_process(rtmp_connection_t* conn);
static void rtmp_handle_connect(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk);
static void rtmp_handle_create_stream(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk);
static void rtmp_handle_play(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk);
static void rtmp_handle_publish(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk);
static void rtmp_handle_video(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk);
static void rtmp_handle_audio(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk);
static void rtmp_handle_metadata(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk);

// Initialize server
bool rtmp_server_initialize(void) {
    memset(&server_ctx, 0, sizeof(server_ctx));
    pthread_mutex_init(&server_ctx.lock, NULL);
    server_ctx.state = RTMP_SERVER_STATE_STOPPED;
    return true;
}

// Start server
bool rtmp_server_start(uint16_t port) {
    if (server_ctx.state != RTMP_SERVER_STATE_STOPPED) {
        return false;
    }

    // Create socket
    server_ctx.listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_ctx.listen_socket < 0) {
        return false;
    }

    // Set socket options
    int reuse = 1;
    if (setsockopt(server_ctx.listen_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        close(server_ctx.listen_socket);
        return false;
    }

    // Bind socket
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(server_ctx.listen_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server_ctx.listen_socket);
        return false;
    }

    // Listen for connections
    if (listen(server_ctx.listen_socket, RTMP_MAX_CONNECTIONS) < 0) {
        close(server_ctx.listen_socket);
        return false;
    }

    // Start threads
    server_ctx.running = true;
    server_ctx.port = port;

    if (pthread_create(&server_ctx.accept_thread, NULL, rtmp_server_accept_thread, NULL) != 0) {
        close(server_ctx.listen_socket);
        server_ctx.running = false;
        return false;
    }

    if (pthread_create(&server_ctx.monitor_thread, NULL, rtmp_server_monitor_thread, NULL) != 0) {
        server_ctx.running = false;
        pthread_join(server_ctx.accept_thread, NULL);
        close(server_ctx.listen_socket);
        return false;
    }

    rtmp_server_update_state(RTMP_SERVER_STATE_RUNNING);
    return true;
}

// Accept thread function
static void* rtmp_server_accept_thread(void* arg) {
    while (server_ctx.running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int client_socket = accept(server_ctx.listen_socket, (struct sockaddr*)&client_addr, &addr_len);
        if (client_socket < 0) {
            if (errno != EINTR) {
                // Log error
                continue;
            }
            continue;
        }

        // Create new connection
        rtmp_connection_t* conn = calloc(1, sizeof(rtmp_connection_t));
        if (!conn) {
            close(client_socket);
            continue;
        }

        conn->socket = client_socket;
        conn->state = RTMP_CONN_STATE_NEW;
        gettimeofday(&conn->last_recv_time, NULL);
        gettimeofday(&conn->last_send_time, NULL);

        // Add to connection list
        pthread_mutex_lock(&server_ctx.lock);
        conn->next = server_ctx.connections;
        server_ctx.connections = conn;
        server_ctx.num_connections++;
        pthread_mutex_unlock(&server_ctx.lock);

        // Notify callback
        if (connection_callback) {
            connection_callback(conn, connection_callback_data);
        }

        // Handle connection in new thread
        pthread_t thread;
        if (pthread_create(&thread, NULL, (void*(*)(void*))rtmp_server_handle_connection, conn) != 0) {
            rtmp_server_cleanup_connection(conn);
            continue;
        }
        pthread_detach(thread);
    }

    return NULL;
}

// Monitor thread function
static void* rtmp_server_monitor_thread(void* arg) {
    while (server_ctx.running) {
        pthread_mutex_lock(&server_ctx.lock);
        
        struct timeval now;
        gettimeofday(&now, NULL);

        rtmp_connection_t* prev = NULL;
        rtmp_connection_t* conn = server_ctx.connections;

        while (conn) {
            rtmp_connection_t* next = conn->next;
            
            // Check timeout
            if (now.tv_sec - conn->last_recv_time.tv_sec > RTMP_TIMEOUT_SEC) {
                // Remove from list
                if (prev) {
                    prev->next = next;
                } else {
                    server_ctx.connections = next;
                }
                server_ctx.num_connections--;
                
                // Cleanup connection
                rtmp_server_cleanup_connection(conn);
            } else {
                prev = conn;
            }
            
            conn = next;
        }

        pthread_mutex_unlock(&server_ctx.lock);
        sleep(1);
    }

    return NULL;
}

// Handle client connection
static bool rtmp_server_handle_connection(rtmp_connection_t* conn) {
    if (!conn) return false;

    // Set non-blocking
    int flags = fcntl(conn->socket, F_GETFL, 0);
    fcntl(conn->socket, F_SETFL, flags | O_NONBLOCK);

    // Initialize chunk stream
    conn->chunk_stream = rtmp_chunk_stream_create();
    if (!conn->chunk_stream) {
        rtmp_server_cleanup_connection(conn);
        return false;
    }

    // Handshake
    if (!rtmp_handshake_process(conn)) {
        rtmp_server_cleanup_connection(conn);
        return false;
    }

    conn->state = RTMP_CONN_STATE_HANDSHAKE;

    // Main connection loop
    while (server_ctx.running) {
        // Receive chunks
        if (!rtmp_connection_receive_chunk(conn)) {
            break;
        }

        // Process chunks
        rtmp_chunk_stream_t* chunk;
        while ((chunk = rtmp_chunk_stream_get_next(conn->chunk_stream)) != NULL) {
            rtmp_connection_handle_message(conn, chunk);
        }

        // Update timestamps
        gettimeofday(&conn->last_recv_time, NULL);
    }

    rtmp_server_cleanup_connection(conn);
    return true;
}

// Cleanup connection
static void rtmp_server_cleanup_connection(rtmp_connection_t* conn) {
    if (!conn) return;

    // Close socket
    if (conn->socket >= 0) {
        close(conn->socket);
    }

    // Free chunk stream
    if (conn->chunk_stream) {
        rtmp_chunk_stream_destroy(conn->chunk_stream);
    }

    // Free handshake data
    if (conn->handshake_data) {
        free(conn->handshake_data);
    }

    // Remove from list if still there
    pthread_mutex_lock(&server_ctx.lock);
    rtmp_connection_t* prev = NULL;
    rtmp_connection_t* current = server_ctx.connections;
    
    while (current) {
        if (current == conn) {
            if (prev) {
                prev->next = current->next;
            } else {
                server_ctx.connections = current->next;
            }
            server_ctx.num_connections--;
            break;
        }
        prev = current;
        current = current->next;
    }
    
    pthread_mutex_unlock(&server_ctx.lock);

    // Notify callback
    if (connection_callback) {
        connection_callback(conn, connection_callback_data);
    }

    free(conn);
}

// Update server state with notification
static void rtmp_server_update_state(rtmp_server_state_t new_state) {
    server_ctx.state = new_state;
    if (state_callback) {
        state_callback(new_state, state_callback_data);
    }
}

// Process RTMP handshake
static bool rtmp_handshake_process(rtmp_connection_t* conn) {
    uint8_t c0c1[1537]; // C0 + C1
    uint8_t s0s1s2[3073]; // S0 + S1 + S2
    ssize_t bytes;

    // Receive C0+C1
    bytes = recv(conn->socket, c0c1, sizeof(c0c1), MSG_WAITALL);
    if (bytes != sizeof(c0c1)) {
        return false;
    }

    // Verify C0
    if (c0c1[0] != 3) {
        return false;
    }

    // Generate S0+S1+S2
    s0s1s2[0] = 3; // S0 - version
    
    // S1 - timestamp + zero + random bytes
    uint32_t timestamp = (uint32_t)time(NULL);
    memcpy(s0s1s2 + 1, &timestamp, 4);
    memset(s0s1s2 + 5, 0, 4);
    for (int i = 9; i < 1537; i++) {
        s0s1s2[i] = rand() % 256;
    }

    // S2 - echo client's timestamp + time2 + random echo
    memcpy(s0s1s2 + 1537, c0c1 + 1, 1536);

    // Send S0+S1+S2
    bytes = send(conn->socket, s0s1s2, sizeof(s0s1s2), 0);
    if (bytes != sizeof(s0s1s2)) {
        return false;
    }

    // Receive C2
    uint8_t c2[1536];
    bytes = recv(conn->socket, c2, sizeof(c2), MSG_WAITALL);
    if (bytes != sizeof(c2)) {
        return false;
    }

    return true;
}

// Handle received RTMP message
static void rtmp_connection_handle_message(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk) {
    switch (chunk->msg_type_id) {
        case RTMP_MSG_CONNECT:
            rtmp_handle_connect(conn, chunk);
            break;
            
        case RTMP_MSG_CREATE_STREAM:
            rtmp_handle_create_stream(conn, chunk);
            break;
            
        case RTMP_MSG_PLAY:
            rtmp_handle_play(conn, chunk);
            break;
            
        case RTMP_MSG_PUBLISH:
            rtmp_handle_publish(conn, chunk);
            break;
            
        case RTMP_MSG_VIDEO:
            rtmp_handle_video(conn, chunk);
            break;
            
        case RTMP_MSG_AUDIO:
            rtmp_handle_audio(conn, chunk);
            break;
            
        case RTMP_MSG_DATA:
        case RTMP_MSG_METADATA:
            rtmp_handle_metadata(conn, chunk);
            break;
    }
}

// Handle connect command
static void rtmp_handle_connect(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk) {
    // Parse connect parameters
    rtmp_amf_data_t* amf = rtmp_amf_decode(chunk->msg_data, chunk->msg_length);
    if (!amf) return;

    // Get app name
    rtmp_amf_data_t* app = rtmp_amf_get_prop(amf, "app");
    if (app && app->type == RTMP_AMF_STRING) {
        strncpy(conn->metadata.app_name, app->string_data, sizeof(conn->metadata.app_name)-1);
    }

    // Send Window Acknowledgement Size
    uint8_t ack_size[4];
    uint32_t window_size = RTMP_BUFFER_SIZE;
    ack_size[0] = (window_size >> 24) & 0xff;
    ack_size[1] = (window_size >> 16) & 0xff;
    ack_size[2] = (window_size >> 8) & 0xff;
    ack_size[3] = window_size & 0xff;
    
    rtmp_chunk_stream_t response;
    memset(&response, 0, sizeof(response));
    response.msg_type_id = RTMP_MSG_WINDOW_ACK_SIZE;
    response.msg_stream_id = 0;
    response.msg_length = 4;
    response.msg_data = ack_size;
    
    rtmp_connection_send_chunk(conn, &response);

    // Send Set Peer Bandwidth
    uint8_t peer_bw[5];
    peer_bw[0] = (window_size >> 24) & 0xff;
    peer_bw[1] = (window_size >> 16) & 0xff;
    peer_bw[2] = (window_size >> 8) & 0xff;
    peer_bw[3] = window_size & 0xff;
    peer_bw[4] = 2; // Dynamic
    
    response.msg_type_id = RTMP_MSG_SET_PEER_BW;
    response.msg_length = 5;
    response.msg_data = peer_bw;
    
    rtmp_connection_send_chunk(conn, &response);

    // Send Stream Begin
    uint8_t stream_begin[6] = {0,0,0,0,0,0};
    response.msg_type_id = RTMP_MSG_USER_CONTROL;
    response.msg_length = 6;
    response.msg_data = stream_begin;
    
    rtmp_connection_send_chunk(conn, &response);

    // Send connect response
    uint8_t connect_resp[256];
    size_t resp_len = rtmp_amf_encode_connect_response(connect_resp, sizeof(connect_resp));
    
    response.msg_type_id = RTMP_MSG_COMMAND;
    response.msg_length = resp_len;
    response.msg_data = connect_resp;
    
    rtmp_connection_send_chunk(conn, &response);

    conn->state = RTMP_CONN_STATE_CONNECT;
    rtmp_amf_data_free(amf);
}

// Handle create stream command
static void rtmp_handle_create_stream(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk) {
    rtmp_amf_data_t* amf = rtmp_amf_decode(chunk->msg_data, chunk->msg_length);
    if (!amf) return;

    // Send create stream response
    uint8_t create_stream_resp[256];
    size_t resp_len = rtmp_amf_encode_create_stream_response(create_stream_resp, sizeof(create_stream_resp));
    
    rtmp_chunk_stream_t response;
    memset(&response, 0, sizeof(response));
    response.msg_type_id = RTMP_MSG_COMMAND;
    response.msg_stream_id = chunk->msg_stream_id;
    response.msg_length = resp_len;
    response.msg_data = create_stream_resp;
    
    rtmp_connection_send_chunk(conn, &response);

    conn->state = RTMP_CONN_STATE_CREATE_STREAM;
    rtmp_amf_data_free(amf);
}

// Handle play command
static void rtmp_handle_play(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk) {
    rtmp_amf_data_t* amf = rtmp_amf_decode(chunk->msg_data, chunk->msg_length);
    if (!amf) return;

    // Get stream name
    rtmp_amf_data_t* stream_name = rtmp_amf_get_prop(amf, "streamName");
    if (stream_name && stream_name->type == RTMP_AMF_STRING) {
        strncpy(conn->metadata.stream_name, stream_name->string_data, sizeof(conn->metadata.stream_name)-1);
    }

    // Send stream begin
    uint8_t stream_begin[6] = {0,0,0,0,0,0};
    rtmp_chunk_stream_t response;
    memset(&response, 0, sizeof(response));
    response.msg_type_id = RTMP_MSG_USER_CONTROL;
    response.msg_stream_id = chunk->msg_stream_id;
    response.msg_length = 6;
    response.msg_data = stream_begin;
    
    rtmp_connection_send_chunk(conn, &response);

    // Send play response
    uint8_t play_resp[256];
    size_t resp_len = rtmp_amf_encode_play_response(play_resp, sizeof(play_resp));
    
    response.msg_type_id = RTMP_MSG_COMMAND;
    response.msg_length = resp_len;
    response.msg_data = play_resp;
    
    rtmp_connection_send_chunk(conn, &response);

    conn->state = RTMP_CONN_STATE_PLAY;
    conn->is_publisher = false;
    rtmp_amf_data_free(amf);
}

// Handle publish command
static void rtmp_handle_publish(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk) {
    rtmp_amf_data_t* amf = rtmp_amf_decode(chunk->msg_data, chunk->msg_length);
    if (!amf) return;

    // Get publish name
    rtmp_amf_data_t* publish_name = rtmp_amf_get_prop(amf, "publishName");
    if (publish_name && publish_name->type == RTMP_AMF_STRING) {
        strncpy(conn->metadata.stream_name, publish_name->string_data, sizeof(conn->metadata.stream_name)-1);
    }

    // Send publish response
    uint8_t publish_resp[256];
    size_t resp_len = rtmp_amf_encode_publish_response(publish_resp, sizeof(publish_resp));
    
    rtmp_chunk_stream_t response;
    memset(&response, 0, sizeof(response));
    response.msg_type_id = RTMP_MSG_COMMAND;
    response.msg_stream_id = chunk->msg_stream_id;
    response.msg_length = resp_len;
    response.msg_data = publish_resp;
    
    rtmp_connection_send_chunk(conn, &response);

    conn->state = RTMP_CONN_STATE_PUBLISHING;
    conn->is_publisher = true;
    gettimeofday(&conn->metadata.publish_time, NULL);
    rtmp_amf_data_free(amf);
}

// Handle video data
static void rtmp_handle_video(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk) {
    if (!conn->is_publisher || !chunk->msg_data || chunk->msg_length == 0) return;

    conn->metadata.has_video = true;
    conn->metadata.bytes_in += chunk->msg_length;

    // Parse video frame header
    uint8_t frame_type = (chunk->msg_data[0] & 0xf0) >> 4;
    bool is_keyframe = (frame_type == 1);

    // Forward to callback
    if (frame_callback) {
        frame_callback(chunk->msg_data, chunk->msg_length, chunk->timestamp, is_keyframe, frame_callback_data);
    }
}

// Handle audio data
static void rtmp_handle_audio(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk) {
    if (!conn->is_publisher || !chunk->msg_data || chunk->msg_length == 0) return;

    conn->metadata.has_audio = true;
    conn->metadata.bytes_in += chunk->msg_length;

    // Parse audio header
    uint8_t format = (chunk->msg_data[0] & 0xf0) >> 4;
    uint8_t rate = (chunk->msg_data[0] & 0x0c) >> 2;
    uint8_t size = (chunk->msg_data[0] & 0x02) >> 1;
    uint8_t type = chunk->msg_data[0] & 0x01;

    // Update metadata
    conn->metadata.audio_sample_rate = rate;
    conn->metadata.audio_channels = type + 1;
}

// Handle metadata
static void rtmp_handle_metadata(rtmp_connection_t* conn, rtmp_chunk_stream_t* chunk) {
    if (!conn->is_publisher || !chunk->msg_data || chunk->msg_length == 0) return;

    rtmp_amf_data_t* amf = rtmp_amf_decode(chunk->msg_data, chunk->msg_length);
    if (!amf) return;

    // Parse metadata values
    rtmp_amf_data_t* width = rtmp_amf_get_prop(amf, "width");
    if (width && width->type == RTMP_AMF_NUMBER) {
        conn->metadata.width = (uint32_t)width->number_data;
    }

    rtmp_amf_data_t* height = rtmp_amf_get_prop(amf, "height");
    if (height && height->type == RTMP_AMF_NUMBER) {
        conn->metadata.height = (uint32_t)height->number_data;
    }

    rtmp_amf_data_t* framerate = rtmp_amf_get_prop(amf, "framerate");
    if (framerate && framerate->type == RTMP_AMF_NUMBER) {
        conn->metadata.frame_rate = (uint32_t)framerate->number_data;
    }

    rtmp_amf_data_t* videodatarate = rtmp_amf_get_prop(amf, "videodatarate");
    if (videodatarate && videodatarate->type == RTMP_AMF_NUMBER) {
        conn->metadata.video_bitrate = (uint32_t)(videodatarate->number_data * 1024);
    }

    rtmp_amf_data_t* audiodatarate = rtmp_amf_get_prop(amf, "audiodatarate");
    if (audiodatarate && audiodatarate->type == RTMP_AMF_NUMBER) {
        conn->metadata.audio_bitrate = (uint32_t)(audiodatarate->number_data * 1024);
    }

    // Notify callback
    if (metadata_callback) {
        metadata_callback(&conn->metadata, metadata_callback_data);
    }

    rtmp_amf_data_free(amf);
}

// Public API implementations
// Stop server
void rtmp_server_stop(void) {
    if (!server_ctx.running) return;

    // Stop threads
    server_ctx.running = false;
    
    // Close listen socket to break accept thread
    if (server_ctx.listen_socket >= 0) {
        close(server_ctx.listen_socket);
        server_ctx.listen_socket = -1;
    }

    // Wait for threads to finish
    pthread_join(server_ctx.accept_thread, NULL);
    pthread_join(server_ctx.monitor_thread, NULL);

    // Close all connections
    pthread_mutex_lock(&server_ctx.lock);
    while (server_ctx.connections) {
        rtmp_connection_t* conn = server_ctx.connections;
        server_ctx.connections = conn->next;
        rtmp_server_cleanup_connection(conn);
    }
    pthread_mutex_unlock(&server_ctx.lock);

    rtmp_server_update_state(RTMP_SERVER_STATE_STOPPED);
}

// Cleanup server resources
void rtmp_server_cleanup(void) {
    rtmp_server_stop();
    pthread_mutex_destroy(&server_ctx.lock);
}

// Get server state
rtmp_server_state_t rtmp_server_get_state(void) {
    return server_ctx.state;
}

// Get connection by socket
rtmp_connection_t* rtmp_server_get_connection(int socket) {
    rtmp_connection_t* conn = NULL;
    
    pthread_mutex_lock(&server_ctx.lock);
    for (conn = server_ctx.connections; conn; conn = conn->next) {
        if (conn->socket == socket) break;
    }
    pthread_mutex_unlock(&server_ctx.lock);
    
    return conn;
}

// Close specific connection
void rtmp_server_close_connection(rtmp_connection_t* conn) {
    if (!conn) return;
    rtmp_server_cleanup_connection(conn);
}

// Get number of active connections
uint32_t rtmp_server_get_num_connections(void) {
    return server_ctx.num_connections;
}

// Check if stream is being published
bool rtmp_server_is_publishing(const char* stream_name) {
    bool publishing = false;
    
    pthread_mutex_lock(&server_ctx.lock);
    for (rtmp_connection_t* conn = server_ctx.connections; conn; conn = conn->next) {
        if (conn->is_publisher && strcmp(conn->metadata.stream_name, stream_name) == 0) {
            publishing = true;
            break;
        }
    }
    pthread_mutex_unlock(&server_ctx.lock);
    
    return publishing;
}

// Set callbacks
void rtmp_server_set_connection_callback(rtmp_connection_callback_t callback, void* userdata) {
    connection_callback = callback;
    connection_callback_data = userdata;
}

void rtmp_server_set_metadata_callback(rtmp_metadata_callback_t callback, void* userdata) {
    metadata_callback = callback;
    metadata_callback_data = userdata;
}

void rtmp_server_set_frame_callback(rtmp_frame_callback_t callback, void* userdata) {
    frame_callback = callback;
    frame_callback_data = userdata;
}

void rtmp_server_set_state_callback(rtmp_server_state_callback_t callback, void* userdata) {
    state_callback = callback;
    state_callback_data = userdata;
}

// Get stream info
bool rtmp_server_get_stream_info(const char* stream_name, rtmp_stream_metadata_t* info) {
    if (!stream_name || !info) return false;
    bool found = false;
    
    pthread_mutex_lock(&server_ctx.lock);
    for (rtmp_connection_t* conn = server_ctx.connections; conn; conn = conn->next) {
        if (strcmp(conn->metadata.stream_name, stream_name) == 0) {
            memcpy(info, &conn->metadata, sizeof(rtmp_stream_metadata_t));
            found = true;
            break;
        }
    }
    pthread_mutex_unlock(&server_ctx.lock);
    
    return found;
}

// Get server statistics
uint64_t rtmp_server_get_bytes_received(void) {
    uint64_t total = 0;
    
    pthread_mutex_lock(&server_ctx.lock);
    for (rtmp_connection_t* conn = server_ctx.connections; conn; conn = conn->next) {
        total += conn->metadata.bytes_in;
    }
    pthread_mutex_unlock(&server_ctx.lock);
    
    return total;
}

uint64_t rtmp_server_get_bytes_sent(void) {
    uint64_t total = 0;
    
    pthread_mutex_lock(&server_ctx.lock);
    for (rtmp_connection_t* conn = server_ctx.connections; conn; conn = conn->next) {
        total += conn->metadata.bytes_out;
    }
    pthread_mutex_unlock(&server_ctx.lock);
    
    return total;
}

uint32_t rtmp_server_get_dropped_frames(void) {
    uint32_t total = 0;
    
    pthread_mutex_lock(&server_ctx.lock);
    for (rtmp_connection_t* conn = server_ctx.connections; conn; conn = conn->next) {
        total += conn->metadata.dropped_frames;
    }
    pthread_mutex_unlock(&server_ctx.lock);
    
    return total;
}

// Configuration functions
void rtmp_server_set_chunk_size(uint32_t size) {
    uint8_t msg[4];
    msg[0] = (size >> 24) & 0xff;
    msg[1] = (size >> 16) & 0xff;
    msg[2] = (size >> 8) & 0xff;
    msg[3] = size & 0xff;
    
    rtmp_chunk_stream_t chunk;
    memset(&chunk, 0, sizeof(chunk));
    chunk.msg_type_id = RTMP_MSG_SET_CHUNK_SIZE;
    chunk.msg_stream_id = 0;
    chunk.msg_length = 4;
    chunk.msg_data = msg;
    
    pthread_mutex_lock(&server_ctx.lock);
    for (rtmp_connection_t* conn = server_ctx.connections; conn; conn = conn->next) {
        rtmp_connection_send_chunk(conn, &chunk);
    }
    pthread_mutex_unlock(&server_ctx.lock);
}

void rtmp_server_set_window_ack_size(uint32_t size) {
    uint8_t msg[4];
    msg[0] = (size >> 24) & 0xff;
    msg[1] = (size >> 16) & 0xff;
    msg[2] = (size >> 8) & 0xff;
    msg[3] = size & 0xff;
    
    rtmp_chunk_stream_t chunk;
    memset(&chunk, 0, sizeof(chunk));
    chunk.msg_type_id = RTMP_MSG_WINDOW_ACK_SIZE;
    chunk.msg_stream_id = 0;
    chunk.msg_length = 4;
    chunk.msg_data = msg;
    
    pthread_mutex_lock(&server_ctx.lock);
    for (rtmp_connection_t* conn = server_ctx.connections; conn; conn = conn->next) {
        rtmp_connection_send_chunk(conn, &chunk);
    }
    pthread_mutex_unlock(&server_ctx.lock);
}

void rtmp_server_set_peer_bandwidth(uint32_t window_size, uint8_t limit_type) {
    uint8_t msg[5];
    msg[0] = (window_size >> 24) & 0xff;
    msg[1] = (window_size >> 16) & 0xff;
    msg[2] = (window_size >> 8) & 0xff;
    msg[3] = window_size & 0xff;
    msg[4] = limit_type;
    
    rtmp_chunk_stream_t chunk;
    memset(&chunk, 0, sizeof(chunk));
    chunk.msg_type_id = RTMP_MSG_SET_PEER_BW;
    chunk.msg_stream_id = 0;
    chunk.msg_length = 5;
    chunk.msg_data = msg;
    
    pthread_mutex_lock(&server_ctx.lock);
    for (rtmp_connection_t* conn = server_ctx.connections; conn; conn = conn->next) {
        rtmp_connection_send_chunk(conn, &chunk);
    }
    pthread_mutex_unlock(&server_ctx.lock);
}

// Utility functions
const char* rtmp_server_state_string(rtmp_server_state_t state) {
    switch (state) {
        case RTMP_SERVER_STATE_STOPPED:
            return "Stopped";
        case RTMP_SERVER_STATE_STARTING:
            return "Starting";
        case RTMP_SERVER_STATE_RUNNING:
            return "Running";
        case RTMP_SERVER_STATE_ERROR:
            return "Error";
        case RTMP_SERVER_STATE_RESTARTING:
            return "Restarting";
        default:
            return "Unknown";
    }
}

const char* rtmp_connection_state_string(rtmp_connection_state_t state) {
    switch (state) {
        case RTMP_CONN_STATE_NEW:
            return "New";
        case RTMP_CONN_STATE_HANDSHAKE:
            return "Handshake";
        case RTMP_CONN_STATE_CONNECT:
            return "Connect";
        case RTMP_CONN_STATE_CREATE_STREAM:
            return "CreateStream";
        case RTMP_CONN_STATE_PLAY:
            return "Play";
        case RTMP_CONN_STATE_PUBLISHING:
            return "Publishing";
        case RTMP_CONN_STATE_CLOSED:
            return "Closed";
        default:
            return "Unknown";
    }
}

void rtmp_server_dump_stats(void) {
    pthread_mutex_lock(&server_ctx.lock);
    
    printf("RTMP Server Statistics:\n");
    printf("  State: %s\n", rtmp_server_state_string(server_ctx.state));
    printf("  Port: %d\n", server_ctx.port);
    printf("  Connections: %d\n", server_ctx.num_connections);
    
    rtmp_connection_t* conn = server_ctx.connections;
    while (conn) {
        printf("  Connection %d:\n", conn->socket);
        printf("    State: %s\n", rtmp_connection_state_string(conn->state));
        printf("    App: %s\n", conn->metadata.app_name);
        printf("    Stream: %s\n", conn->metadata.stream_name);
        printf("    Is Publisher: %s\n", conn->is_publisher ? "Yes" : "No");
        printf("    Bytes In: %lu\n", conn->metadata.bytes_in);
        printf("    Bytes Out: %lu\n", conn->metadata.bytes_out);
        if (conn->is_publisher) {
            printf("    Video: %dx%d @ %d fps\n", 
                   conn->metadata.width,
                   conn->metadata.height,
                   conn->metadata.frame_rate);
            printf("    Video Bitrate: %d kbps\n", conn->metadata.video_bitrate / 1024);
            printf("    Audio Bitrate: %d kbps\n", conn->metadata.audio_bitrate / 1024);
            printf("    Dropped Frames: %d\n", conn->metadata.dropped_frames);
        }
        conn = conn->next;
    }
    
    pthread_mutex_unlock(&server_ctx.lock);
}