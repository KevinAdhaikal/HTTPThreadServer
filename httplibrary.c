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

#include "http1.h"

#define MAX_CLIENTS 10
#define MAX_BUFFER 2048

// private function
int find_crlfcrlf_num(const char* str) {
    const char* result = strstr(str, "\r\n\r\n");
    if (result != NULL) return result - str;
    return -1;
}

int find_char_num(const char* str, char ch_find) {
    if (!str) return -1;
    for (int a = 0; a < strlen(str); a++) {
        if (str[a] == ch_find) return a;
    }
    return -1;
}

static void send_socket(SOCKET socket, char* data, size_t len) {
    size_t current_len = 0;
    while (current_len < len) {
        size_t chunk_size = (len - current_len) < MAX_BUFFER ? (len - current_len) : MAX_BUFFER;
        int sent_bytes = send(socket, data + current_len, chunk_size, 0);

        if (sent_bytes == -1) break;

        current_len += sent_bytes;
    }
}

static void close_socket(SOCKET socket) {
    #ifdef _WIN32
    closesocket(socket); 
    #else
    close(socket);
    #endif
}

static int set_socket_non_blocking(SOCKET socket) {
    #ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode);
    #else
    return fcntl(socket, F_SETFL, O_NONBLOCK);
    #endif
}

#ifdef _WIN32
static void
#else
static void*
#endif
handle_client(void *arg) {
    http_thread_private* http_thread = (http_thread_private*)arg;
    //printf("http_thread: %d\n", http_thread->client_socket);
    
    set_socket_non_blocking(http_thread->client_socket);

    http_event* event = malloc(sizeof(http_event));
    memset(event, 0, sizeof(http_event));

    event->client_socket = http_thread->client_socket;
    size_t valread;
    char buffer[MAX_BUFFER];

    while ((valread = recv(http_thread->client_socket, buffer, MAX_BUFFER, 0)) > 0) {
        if (valread == 0) goto DISCONNECT;
        else if (valread == -1) break;

        event->client_req.data = realloc(event->client_req.data, event->client_req.len + valread);
        memcpy(event->client_req.data + event->client_req.len, buffer, valread);

        event->client_req.len += valread;
    }

    if (event->client_req.len > 0) {
        event->client_req.data[event->client_req.len - 1] = '\0';
        event->question_path_pos = find_char_num(event->client_req.data, '?') + 1;
        if (event->question_path_pos) event->path[event->question_path_pos - 1] = '\0';
        event->body_pos = find_crlfcrlf_num(event->client_req.data) + 4;
    }

    http_thread->handler(event);
    
    DISCONNECT:
    shutdown(http_thread->client_socket, SD_BOTH);
    close_socket(http_thread->client_socket);
    free(arg);

    if (event->client_req.len) free(event->client_req.data);
    free(event);
    #ifdef _WIN32
    _endthread();
    #else
    pthread_exit(NULL);
    #endif    
}

//public function
void http_get_header(http_event* e, const char* header_name, char* dest, size_t dest_len) {
    dest[0] = '\0';
    const char* header_start = strstr(e->client_req.data, header_name);

    if (header_start) {
        header_start += strlen(header_name);
        header_start = strchr(header_start, ':');
        if (header_start) {
            header_start++;
            while (*header_start == ' ') header_start++;
            const char* header_end = strchr(header_start, '\r');
            if (header_end) {
                size_t value_len = header_end - header_start;
                if (value_len < dest_len) {
                    strncpy(dest, header_start, value_len);
                    dest[value_len] = '\0';
                }
            }
        }
    }
}

void http_get_query(http_event* e, const char* param, char* value, size_t value_size) {
    if (!e->question_path_pos) return;
    char* query = e->path + e->question_path_pos;
    char query_copy[strlen(query) + 1];
    memcpy(query_copy, query, strlen(query) + 1);

    char* token = strtok(query_copy, "&");
    while (token != NULL) {
        if (strstr(token, param) == token) {
            const char* value_start = strchr(token, '=');
            if (value_start != NULL) {
                strncpy(value, value_start + 1, value_size);
                value[value_size - 1] = '\0';
                return;
            }
        }
        token = strtok(NULL, "&");
    }

    value[0] = '\0';
}

int http_get_query_to_int(http_event* e, const char* param) {
    char temp_query[5];
    http_get_query(e, param, temp_query, 4);
    return atoi(temp_query);
}

void http_get_cookie(http_event* e, const char *cookie_name, char *dest, size_t dest_len) {
    const char *cookie_start = strstr(e->client_req.data, cookie_name);

    if (cookie_start) {
        cookie_start += strlen(cookie_name);

        if (cookie_start[0] == '=') {
            cookie_start++;

            const char *cookie_value_end = strchr(cookie_start, ';');

            if (!cookie_value_end) cookie_value_end = strchr(cookie_start, '\r');

            if (cookie_value_end) {
                size_t cookie_value_len = cookie_value_end - cookie_start;

                if (cookie_value_len < dest_len) {
                    strncpy(dest, cookie_start, cookie_value_len);
                    dest[cookie_value_len] = '\0';
                    return;
                }
            }
        }
    }

    dest[0] = '\0';
}

void http_send_status(http_event* e, int code, char* desc) {
    if (e->response_state == 0) {
        char temp_string[128];
        size_t str_size = snprintf(temp_string, 127, "HTTP/1.1 %d %s\r\n", code, desc);
        send_socket(e->client_socket, temp_string, str_size);
        e->response_state++;
    }
}

void http_send_header(http_event* e, const char* key, const char* value) {
    if (e->response_state == 0) {
        http_send_status(e, 200, "OK");
        e->response_state++;
    }
    char temp_string[1024];
    size_t str_size = snprintf(temp_string, 1023, "%s: %s\r\n", key, value);

    send_socket(e->client_socket, temp_string, str_size);
    if (e->response_state == 1) e->response_state++;
}

void http_write(http_event* e, char* data, size_t len) {
    if (e->response_state == 0) {
        http_send_status(e, 200, "OK");
        e->response_state++;
    }
    if (e->response_state < 3) {
        send(e->client_socket, "\r\n", 2, 0);
        e->response_state = 3;
    }

    send_socket(e->client_socket, data, len);
}

void http_send_file(http_event* e, char* file_name, char clear_alloc_filename) {
    if (e->response_state == 0) {
        http_send_status(e, 200, "OK");
        e->response_state = 1;
    }

    FILE* fp = fopen(file_name, "rb");

    if (!fp) goto CLOSE_FILE;

    char size_to_string[32];

    fseek(fp, 0, SEEK_END);
    snprintf(size_to_string, 31, "%u", (unsigned)ftell(fp));
    fseek(fp, 0, SEEK_SET);

    http_send_header(e, "Content-Length", size_to_string);

    char temp_data[MAX_BUFFER];
    size_t res = 0;

    send(e->client_socket, "\r\n", 2, 0);
    while((res = fread(temp_data, 1, MAX_BUFFER, fp)) != 0) {
        send(e->client_socket, temp_data, res, 0);
    }

    CLOSE_FILE:
    if (clear_alloc_filename) free(file_name);
    fclose(fp);
}

SOCKET http_init(unsigned short port) {
    #ifdef _WIN32
    WSADATA dat;
    if (WSAStartup(MAKEWORD(2, 2), &dat) != 0) {
        perror("WSAStartup failed");
        return -1;
    }
    #endif

    int server_fd;
    struct sockaddr_in address;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options to reuse address and port
    #ifndef _WIN32
    int opt = 1;

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, 4)) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }
    #endif
    //set_socket_non_blocking(server_fd);

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    // Bind the socket to localhost:8080
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    return server_fd;
}

void http_start(SOCKET server_socket, http_handler handler) {
    while(1) {
        SOCKET client_socket = accept(server_socket, NULL, 0);

        if (client_socket == -1) {
            shutdown(client_socket, SD_BOTH);
            close_socket(client_socket);
        } else {
            http_thread_private* http_thread = malloc(sizeof(http_thread_private));
            http_thread->client_socket = client_socket;
            http_thread->handler = handler;

            #ifdef _WIN32
            _beginthread(handle_client, 0, http_thread);
            #else
            pthread_t thread;
            pthread_create(&thread, NULL, handle_client, http_thread);
            pthread_detach(thread);
            #endif
        }
        
    }
}
