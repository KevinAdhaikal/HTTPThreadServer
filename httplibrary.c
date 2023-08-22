#include "httplibrary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

int find_char_num(const char* str, char ch_find) {
    if (!str) return -1;
    for (int a = 0; a < strlen(str); a++) {
        if (str[a] == ch_find) return a;
    }
    return -1;
}

void http_buffer_init(http_event* e, int initial_capacity) {
    http_buffer* b = &e->server_buffer;
    b->data = (char *)malloc(initial_capacity);
    if (!b->data) {
        perror("Memory allocation error");
        return;
    }
    b->len = 0;
    b->capacity = initial_capacity;
    e->state++;
}

void http_buffer_resize(http_buffer* b, int new_capacity) {
    char *new_data = (char *)realloc(b->data, new_capacity);
    if (!new_data) {
        perror("Memory allocation error");
        return;
    }

    b->data = new_data;
    b->capacity = new_capacity;
}

void http_buffer_append(http_event* e, const char *data, int len) {
    if (e->state == 0) http_buffer_init(e, 4096);
    http_buffer* b = &e->server_buffer;

    if (b->len + len > b->capacity) {
        int new_capacity = b->capacity == 0 ? len : b->capacity * 2;
        while (b->len + len > new_capacity) {
            new_capacity *= 2;
        }
        http_buffer_resize(b, new_capacity);
    }

    memcpy(b->data + b->len, data, len);
    b->len += len;
}

void http_buffer_free(http_buffer* b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->capacity = 0;
}

void http_send_status(http_event* e, int status, const char *val) {
    if (e->state == 0) http_buffer_init(e, 4096);
    char response[128];
    int response_length = snprintf(response, sizeof(response), "HTTP/1.1 %d %s\r\n", status, val);
    http_buffer_append(e, response, response_length);
    e->state++;
}

void http_send_header(http_event* e, const char *name, const char *val) {
    if (e->state == 0) http_buffer_init(e, 4096);
    if (e->state == 1) http_send_status(e, 200, "OK");
    char header[256];
    int header_length = snprintf(header, sizeof(header), "%s: %s\r\n", name, val);
    http_buffer_append(e, header, header_length);
    if (e->state < 1) e->state++;
}

void http_write(http_event* e, const char *data, int len) {
    if (e->state == 0) http_buffer_init(e, 4096);
    if (e->state == 1) http_send_status(e, 200, "OK");
    if (e->state == 2) http_buffer_append(e, "\r\n", 2), e->state++;
    http_buffer_append(e, data, !len ? strlen(data) : len);
}

int http_send_file(http_event* e, const char* filename) {
    FILE* fp = fopen(filename, "rb");
    if (!fp) return -1;
    else {
        char toString[32];
        fseek(fp, 0, SEEK_END);
        if (e->state == 0) http_buffer_init(e, 502);
        if (e->state == 1) http_send_status(e, 200, "OK");
        sprintf(toString, "%u", (unsigned)ftell(fp));

        http_send_header(e, "Content-Length", toString);
        http_buffer_append(e, "\r\n", 2);
        fseek(fp, 0, SEEK_SET);

        char tempBuffer[1024];
        size_t bufferSize;
        while(bufferSize = fread(tempBuffer, 1, 1024, fp)) {
            send(e->client_sock, tempBuffer, bufferSize, 0);
        }
        http_buffer_free(&e->server_buffer);
        fclose(fp);
    }
}

void http_get_header(http_event* e, const char* header_name, char* dest, size_t destLen) {
    const char* header_start = strstr(e->headers.raw_header, header_name);

    if (header_start) {
        header_start += strlen(header_name);
        header_start = strchr(header_start, ':');
        if (header_start) {
            header_start++;
            while (*header_start == ' ') header_start++;
            const char* header_end = strchr(header_start, '\r');
            if (header_end) {
                size_t value_len = header_end - header_start;
                if (value_len < destLen) {
                    strncpy(dest, header_start, value_len);
                    dest[value_len] = '\0';
                }
            }
        }
    }
}


void *client_handler(void *arg) {
    http_thread_args *args = (http_thread_args *)arg;

    http_event event = {0};
    char tempReq[4096];
    int tempLen = 0;

    while(1) {
        tempLen = recv(args->client_socket, tempReq, 4095, 0);

        if (tempLen == 0) {
            #ifdef _WIN32
            closesocket(args->client_socket);
            #else
            close(args->client_socket);
            #endif
            free(args);

            if (event.headers.raw_header) free(event.headers.raw_header);
            return NULL;
        }
        else if (tempLen < 0) break;

        event.headers.raw_header = realloc(event.headers.raw_header, event.headers.raw_len + tempLen + 1);
        memcpy(event.headers.raw_header + event.headers.raw_len, tempReq, tempLen);
        event.headers.raw_len += tempLen;
        event.headers.raw_header[event.headers.raw_len] = '\0';
    }

    if (event.headers.raw_len > 0) {
        int method_len = find_char_num(event.headers.raw_header, ' ') + 1;
        int path_len = find_char_num(event.headers.raw_header + method_len, ' ');
        strncpy(event.headers.method, event.headers.raw_header, method_len);
        strncpy(event.headers.path, event.headers.raw_header + method_len, path_len);
        event.headers.method[method_len] = '\0';
        event.headers.path[path_len] = '\0';
    }
    else {
        #ifdef _WIN32
        closesocket(args->client_socket);
        #else
        close(args->client_socket);
        #endif
        free(args);
        if (event.headers.raw_header) free(event.headers.raw_header);
        return NULL;
    }

    event.client_sock = args->client_socket;
    args->callback(&event);

    if (event.server_buffer.len) {
        send(event.client_sock, event.server_buffer.data, event.server_buffer.len, 0);
        http_buffer_free(&event.server_buffer);
    }

    if (event.headers.raw_header) {
        free(event.headers.raw_header);
    }

    #ifdef _WIN32
        closesocket(args->client_socket);
    #else
        close(args->client_socket);
    #endif
    free(args);
    return NULL;
}

int httpInit(short port) {
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        perror("Error initializing Winsock");
        return -1;
    }
#endif

    int server_socket;
    struct sockaddr_in server_addr;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);

    if (server_socket == -1) {
        perror("Socket creation failed");
#ifdef _WIN32
        WSACleanup();
#endif
        return -1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Bind failed");
#ifdef _WIN32
        closesocket(server_socket);
        WSACleanup();
#else
        close(server_socket);
#endif
        return -1;
    }

    if (listen(server_socket, 10) == -1) {
        perror("Listen failed");
#ifdef _WIN32
        closesocket(server_socket);
        WSACleanup();
#else
        close(server_socket);
#endif
        return -1;
    }

    return server_socket;
}

void httpRun(int server_socket, http_callback callback) {
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);

        #ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(client_socket, FIONBIO, &mode);
        #else
        int flags = fcntl(client_socket, F_GETFL);
        fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);
        #endif

        http_thread_args *args = (http_thread_args *)malloc(sizeof(http_thread_args));
        args->client_socket = client_socket;
        args->callback = callback;

        pthread_t thread;
        if (pthread_create(&thread, NULL, client_handler, args) != 0) {
            perror("Thread creation failed");
            free(args);
#ifdef _WIN32
            closesocket(client_socket);
#else
            close(client_socket);
#endif
            continue;
        }

        pthread_detach(thread);
    }

#ifdef _WIN32
    closesocket(server_socket);
    WSACleanup();
#else
    close(server_socket);
#endif
}
