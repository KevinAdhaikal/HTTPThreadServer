#ifndef HTTP_HEADER_INCLUDED
#define HTTP_HEADER_INCLUDED

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <ctype.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#else
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#include <ctype.h>
#include <errno.h>

#define SOCKET int
#define SD_RECEIVE SHUT_RD
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR
#endif

#include "buffer_lib.h"

typedef size_t cmp_stream_stat;
#define HTTP_TEMPORARY_MAX_BUFFER 8192

typedef struct {
    char* val;
    size_t len;
} http_string;

typedef struct {
    SOCKET socket;
    
    // Parsing result
    http_string method;
    http_string path;
    char* query_pointer;
    http_string version;
    char* headers_pointer;
    char* cookie_pointer;
    http_string body;

    struct sockaddr_in addr;
} http_client;

typedef void (*http_callback)(http_client*);

typedef struct {
    #ifndef _WIN32
    pthread_t thread_id;
    #endif
    http_callback callback;
    buffer req_raw_data;
    http_client* client;
    cmp_stream_stat status;
    size_t body_len_remaining;
    char no_need_check_request;
} http_room;

typedef struct {
    char still_on; // memberi tahu bahwa server ini masih on atau off
    size_t max_sockets; // buat nge cek max threadsd
    SOCKET server_socket; // server socket nya
    http_room* thread_rooms; // isi isi room untuk http.
} http;

#define http_get_query(query_pointer, key) __http_get_query(query_pointer, key, sizeof(key) - 1) // HTTP Get Query
#define http_get_header(headers_pointer, key) __http_get_header(headers_pointer, key, sizeof(key) - 1) // HTTP Get Header
char* __http_get_query(char* query_pointer, const char* key, size_t key_len); // HTTP Get Query
char* __http_get_header(char* headers_pointer, const char* key, size_t key_len); // HTP Get Header
char* http_get_cookie(http_client* client, const char* key); // HTTP Get Cookie
char http_write(http_client* client, const char* data, size_t size); // HTTP Write
#define http_write_string(client, str) http_write(client, str, sizeof(str) - 1) // HTTP Write String
char http_send_file(http_client* client, const char* name_file, char manual_code, char use_cache); // HTTP Send File

http* http_init_socket(const char* ip, unsigned short port, size_t max_threads, http_callback callback); // initilaize socket
void http_start(http* http); // Start HTTP
void http_stop(http* http); // Stop HTTP

#endif // HTTP_HEADER_INCLUDED
