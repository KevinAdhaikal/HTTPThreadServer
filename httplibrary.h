#ifndef HTTP_H_INCLUDE
#define HTTP_H_INCLUDE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

typedef int SOCKET;
#endif

typedef struct {
    char* data;
    size_t len;
} http_buffer;

typedef struct {
    SOCKET client_socket;
    http_buffer client_req;
    
    char response_state;
    char method[32];
    char path[2048];

    size_t question_path_pos;
    size_t body_pos;
} http_event;

typedef void (*http_handler)(http_event*);

typedef struct {
    SOCKET client_socket;
    http_handler handler;
} http_thread_private;

void http_send_status(http_event* e, int code, char* desc);
void http_send_header(http_event* e, const char* key, const char* value);
void http_send_file(http_event* e, char* file_name, char clear_alloc_filename);
void http_write(http_event* e, char* data, size_t len);

SOCKET http_init(unsigned short port);
void http_start(SOCKET server_socket, http_handler handler);

#endif // HTTP_H_INCLUDE
