#include "string_lib.h"
#include "httplibrary.h"

#define TEMP_BUFFER_LEN 512
#define MAX_THREAD 10

typedef struct {
    http_client* client;
    http_callback callback;
    char* selected_thread_room;
    #ifdef _WIN32
    HANDLE thread_handle;
    #endif
    string req_raw_data; // HTTP Request raw data
} http_thread;

#ifdef _WIN32
HANDLE ghSemaphore;
#else
sem_t ghSemaphore;
#endif

SOCKET http_init_socket(const char* ip, unsigned short port) {
    // initialize windows socket
    #ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        printf("WSAStartup failed with error\n");
        return 0;
    }
    #endif

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    SOCKET r_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (r_socket == -1) {
        printf("Socket creation failed");
        return -1;
    }

    // Setup the TCP listening socket
    if (bind(r_socket, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_in)) == -1) {
        printf("bind error!\n");
        http_close_socket(r_socket);
        return -1;
    }

    // listen socket
    if (listen(r_socket, SOMAXCONN) == -1) {
        printf("listen error!\n");
        http_close_socket(r_socket);
        return -1;
    }

    #ifdef _WIN32
    if (ioctlsocket(r_socket, FIONBIO, &(u_long){1}) != NO_ERROR) {
        printf("ioctlsocket error!\n");
        http_close_socket(r_socket);
        return -1;
    }
    #else
    if (fcntl(r_socket, F_SETFL, fcntl(r_socket, F_GETFL, 0) | O_NONBLOCK) == -1) {
        printf("fcntl failed\n");
        http_close_socket(r_socket);
        return -1;
    }
    #endif

    setsockopt(r_socket, SOL_SOCKET, SO_REUSEADDR, (const char[4]){1, 0, 0, 0}, 4);
    return r_socket;
}

char* http_get_query(http_client* client, const char* key) {
    size_t key_len = strlen(key), cur_len = 0, cur_list_length;
    char* query_pointer = client->query_pointer;

    while(1) {
        while(1) {
            if (tolower(*query_pointer) != tolower(key[cur_len])) {
                cur_len = 0;
                break;
            } else query_pointer++, cur_len++;
            if (cur_len == key_len) return query_pointer + 1;
        }

        if (client->query_list_length == cur_list_length) return NULL;
        for (; *query_pointer != '\0'; query_pointer++);
        query_pointer++, cur_list_length++;
    }
}

char* http_get_header(http_client* client, const char* key) {
    size_t key_len = strlen(key), cur_len = 0;
    char* headers_pointer = client->headers_pointer;

    while(1) {
        while(1) {
            if (tolower(*headers_pointer) != tolower(key[cur_len])) {
                cur_len = 0;
                break;
            } else headers_pointer++, cur_len++;
            if (cur_len == key_len) return headers_pointer + 2;
        }

        for (; *headers_pointer != '\0'; headers_pointer++);
        if (*(headers_pointer + 1) == '\r' || *(headers_pointer + 1) == '\0') return NULL;
        headers_pointer++;
    }
}

char* http_get_cookie(http_client* client, const char* key) {
    char* cookie_pointer = client->cookie_pointer;
    if (cookie_pointer == NULL) { // kita parsing dulu, jika cookie_pointer nya kosong
        client->cookie_pointer = http_get_header(client, "cookie");
        cookie_pointer = client->cookie_pointer;
        if (cookie_pointer == NULL) return NULL;

        while(*cookie_pointer != '\0' && *cookie_pointer != '\1') {
            if (*cookie_pointer == ';') {
                *cookie_pointer++ = '\0';
                if (*cookie_pointer == ' ') *cookie_pointer++ = '\2';
            } else cookie_pointer++;
        }
        cookie_pointer = client->cookie_pointer;
    }

    size_t cur_len = 0, key_len = strlen(key);
    while(1) {
        while(1) {
            if (tolower(*cookie_pointer) != tolower(key[cur_len])) {
                cur_len = 0;
                break;
            } else cookie_pointer++, cur_len++;
            if (cur_len == key_len) return cookie_pointer + 1;
        }

        for (; *cookie_pointer != '\0'; cookie_pointer++);
        if (*++cookie_pointer == '\2') cookie_pointer++;
        if (*cookie_pointer == '\0' || *cookie_pointer == '\1') return NULL;
    }
}

#ifdef _WIN32
static DWORD
#else
static void*
#endif
http_handle_client(void* arg) {
    http_thread* thread = arg;
    http_client* client = thread->client;

    string_init(&thread->req_raw_data);
    char recvbuf[TEMP_BUFFER_LEN];
    unsigned int recv_res = 0;

    struct timeval tv;
    fd_set recvfds;
    FD_ZERO(&recvfds);
    FD_SET(client->socket, &recvfds);

    while(1) {
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        if (!select(client->socket + 1, &recvfds, NULL, NULL, &tv)) goto THREAD_EXIT;
        else if (FD_ISSET(client->socket, &recvfds)) {
            while (1) {
                recv_res = recv(client->socket, recvbuf, TEMP_BUFFER_LEN, 0);
                if (!recv_res) goto THREAD_EXIT;
                else if (recv_res == -1) break;
                else if (recv_res > 0) string_add_n(&thread->req_raw_data, recvbuf, recv_res);
            }
            break;
        }
    }

    // HTTP request parser
    char* cur_pos = thread->req_raw_data.val;

    // HTTP Method
    client->method.val = cur_pos;
    for (; *cur_pos != ' '; cur_pos++) client->method.len++;
    *cur_pos++ = '\0';

    // HTTP Path
    client->path.val = cur_pos;
    for (; *cur_pos != ' '; cur_pos++, client->path.len++) {
        // query parsing
        if (*cur_pos == '&' && client->query_pointer != NULL) *cur_pos = '\0', client->query_list_length++;
        else if (*cur_pos == '?') *cur_pos++ = '\0', client->query_pointer = cur_pos;
    }
    *cur_pos++ = '\0';

    // HTTP Version
    client->version.val = cur_pos;
    for (; *cur_pos != '\n'; cur_pos++) client->version.len++;
    *cur_pos++ = '\0';

    // HTTP Headers
    client->headers_pointer = cur_pos;
    char* s_header_pos = cur_pos; // start header pos

    while(1) {
        if (*cur_pos == '\n') {
            *cur_pos++ = 0;

            if (*cur_pos == '\r' && *(cur_pos + 1) == '\n') {
                *++cur_pos = 0;
                break;
            }
            if (*cur_pos == '\n') {
                *cur_pos++ = 0;
                break;
            }
        }
        cur_pos++;
    }
    client->headers_length = cur_pos - s_header_pos;
    
    // HTTP Body
    client->body.val = cur_pos;
    client->body.len = thread->req_raw_data.len - (cur_pos - thread->req_raw_data.val);
    
    thread->callback(client);

    shutdown(client->socket, SD_BOTH);

    THREAD_EXIT:
    http_close_socket(client->socket);
    string_finalize(&thread->req_raw_data);
    
    #ifdef _WIN32
    ReleaseSemaphore(ghSemaphore, 1, NULL);
    #else
    sem_post(&ghSemaphore);
    #endif

    *thread->selected_thread_room = 0;

    #ifdef _WIN32
    CloseHandle(thread->thread_handle);
    #endif
    return 0;
}

void http_start(SOCKET s_socket, http_callback callback) {
    #ifdef _WIN32
    ghSemaphore = CreateSemaphore(
        NULL,        // default security attributes
        MAX_THREAD,  // initial count
        MAX_THREAD,  // maximum count
        NULL         // unnamed semaphore
    );
    #else
    sem_init(&ghSemaphore, 0, MAX_THREAD);
    #endif

    SOCKET c_socket;
    
    http_thread thread[MAX_THREAD];
    http_client client[MAX_THREAD];

    char* thread_room = malloc(MAX_THREAD);
    memset(thread_room, 0, MAX_THREAD);
    memset(client, 0, sizeof(http_client) * MAX_THREAD);
    memset(thread, 0, sizeof(http_thread) * MAX_THREAD);

    unsigned int current_thread_id = 0;

    fd_set readfds;
    struct timeval tv;

    while(1) {
        FD_ZERO(&readfds);

        // Accept a client socket
        while (1) {
            tv.tv_sec = 1;
            tv.tv_usec = 0;

            FD_SET(s_socket, &readfds);
            int sel_val = select(s_socket + 1, &readfds, NULL, NULL, &tv);
            if (sel_val != 0 && FD_ISSET(s_socket, &readfds)) {
                c_socket = accept(s_socket, NULL, NULL);
                break;
            }
        }

        // Set the client socket to non-blocking mode
        #ifdef _WIN32
        ioctlsocket(c_socket, FIONBIO, &(u_long){1});
        #else
        fcntl(c_socket, F_SETFL, fcntl(c_socket, F_GETFL, 0) | O_NONBLOCK);
        #endif

        #ifdef _WIN32
        if (WaitForSingleObject(ghSemaphore, INFINITE) == WAIT_OBJECT_0) {
        #else
        sem_wait(&ghSemaphore);
        #endif
            while(1) {
                if (current_thread_id >= MAX_THREAD) current_thread_id = 0;
                if (thread_room[current_thread_id]) {
                    current_thread_id++;
                    continue;
                }

                thread_room[current_thread_id] = 1;

                client[current_thread_id].socket = c_socket;

                thread[current_thread_id] = (http_thread){
                    &client[current_thread_id],
                    callback,
                    &thread_room[current_thread_id],
                    #ifdef _WIN32
                    CreateThread(0, 0, http_handle_client, &thread[current_thread_id], 0, NULL),
                    #else
                    NULL,
                    #endif
                    NULL
                };

                #ifndef _WIN32
                pthread_t temp_thread;
                pthread_create(&temp_thread, NULL, http_handle_client, &thread[current_thread_id]);
                pthread_detach(temp_thread);
                #endif

                current_thread_id++;
                break;
            }
        #ifdef _WIN32
        }
        #endif
    }

    #ifdef _WIN32
    CloseHandle(ghSemaphore);
    #else
    sem_destroy(&ghSemaphore);
    #endif
}