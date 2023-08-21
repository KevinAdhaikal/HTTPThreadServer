#ifndef HTTPLIBRARY_H
#define HTTPLIBRARY_H

#include <stdio.h>
#ifdef __WIN32
#include <winsock2.h>
#endif

typedef struct {
    char method[16];
    char path[512];
    char* raw_header;
    int raw_len;
} http_header;

typedef struct {
    char *data;
    int len;
    int capacity;
} http_buffer;

typedef struct {
    int client_sock;
    char state;
    http_header headers;
    http_buffer server_buffer;
} http_event;

typedef void (*http_callback)(http_event*);

typedef struct {
    http_callback callback;
    int client_socket;
} http_thread_args;

int httpInit(short port);
void httpRun(int server_socket, http_callback callback);

void http_buffer_init(http_event* e, int initial_capacity);
void http_buffer_resize(http_buffer* b, int new_capacity);
void http_buffer_append(http_event* e, const char *data, int len);
void http_buffer_free(http_buffer* b);
void http_send_status(http_event* e, int status, const char *val);
void http_send_header(http_event* e, const char *name, const char *val);
void http_write(http_event* e, const char *data, int len);
int http_send_file(http_event* e, const char* filename);

#endif // HTTPLIBRARY_H
