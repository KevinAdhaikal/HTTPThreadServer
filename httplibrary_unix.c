// TODO: Close socket yang idle

#ifndef _WIN32
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>

#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "httplibrary.h"

#define strcmp_last(data, data_len, target) !strcmp(data + (data_len - (sizeof(target) - 1)), target)
#define clean_room(room) \
close(room->client->socket); \
memset(room->client, 0, sizeof(http_client)); \
buffer_begin_reinit(&room->req_raw_data); \
room->no_need_check_request = 0; \
room->body_len_remaining = 0

static char cmp_stream(cmp_stream_stat* status, char* data, size_t data_len, const char* target, size_t target_len, size_t* index_output) {
    size_t match = *status;

    // Jika ada pencocokan parsial sebelumnya, lanjutkan pencarian dari sana
    if (match > 0) {
        size_t remaining = target_len - match;
        if (data_len >= remaining && memcmp(data, target + match, remaining) == 0) {
            *status = 0;
            if (index_output) *index_output = remaining;
            return 2; // Fully found
        }
    }

    // Gunakan `memchr()` untuk menemukan kemungkinan awal `target`
    char* start = memchr(data, target[0], data_len);
    if (!start) {
        *status = 0;
        return 0; // Not found
    }

    size_t offset = start - data;
    while (offset < data_len) {
        size_t remaining = data_len - offset;

        // Jika sisa data cukup untuk target, gunakan `memcmp()` langsung
        if (remaining >= target_len && memcmp(start, target, target_len) == 0) {
            *status = 0;
            if (index_output) *index_output = remaining;
            return 2; // Fully found
        }

        // Periksa pencocokan parsial hanya jika cocok di akhir data
        size_t j = 0;
        while (j < target_len && (offset + j) < data_len && data[offset + j] == target[j]) j++;

        // Pastikan pencocokan parsial hanya valid jika berada di akhir data
        if ((offset + j) == data_len) {
            *status = j;
            return 1; // Partially Found
        }

        // Lanjutkan mencari karakter target pertama di sisa data
        start = memchr(start + 1, target[0], remaining - 1);
        if (!start) break;
        offset = start - data;
    }

    *status = 0;
    return 0; // Not found
}

static unsigned char __private_mime_types(const char* name_file, size_t name_file_len, char* result) {
    if (strcmp_last(name_file, name_file_len, ".html")) {
        memcpy(result, "text/html", 9);
        return 9;
    }
    else if (strcmp_last(name_file, name_file_len,  ".txt")) {
        memcpy(result, "text/plain", 10);
        return 10;
    }
    else if (strcmp_last(name_file, name_file_len, ".js")) {
        memcpy(result, "text/javascript", 15);
        return 15;
    }
    else if (strcmp_last(name_file, name_file_len, ".css")) {
        memcpy(result, "text/css", 8);
        return 8;
    }
    else if (strcmp_last(name_file, name_file_len, ".ico")) {
        memcpy(result, "image/x-icon", 12);
        return 12;
    }
    else if (strcmp_last(name_file, name_file_len, ".woff2")) {
        memcpy(result, "font/woff2", 10);
        return 10;
    }
    else if (strcmp_last(name_file, name_file_len, ".png")) {
        memcpy(result, "image/png", 9);
        return 9;
    }
    else if (strcmp_last(name_file, name_file_len, ".svg")) {
        memcpy(result, "image/svg+xml", 13);
        return 13;
    }
    else if (strcmp_last(name_file, name_file_len, ".jpg") || strcmp_last(name_file, name_file_len, ".jpeg")) {
        memcpy(result, "image/jpeg", 10);
        return 10;
    }
    else {
        memcpy(result, "application/octet-stream", 24);
        return 24;
    }
}

char* __http_get_query(char* query_pointer, const char* key, size_t key_len) {
    while(1) {
        if (strcasecmp(query_pointer, key) == '=') return query_pointer + (key_len + 1);
        query_pointer = strchr(query_pointer, '\0') + 1;
        if (*query_pointer != '&') return NULL;
    }
}

char* __http_get_header(char* headers_pointer, const char* key, size_t key_len) {
    while(1) {
        if (strcasecmp(headers_pointer, key) == ':') {
            return headers_pointer + (key_len + 7);
        }
        headers_pointer = strchr(headers_pointer, '\0') + 2;
        if (*headers_pointer == 0) return NULL;
    }
}

char* http_get_cookie(http_client* client, const char* key) {
    char* cookie_pointer = client->cookie_pointer;
    if (cookie_pointer == NULL) { // kita parsing dulu jika cookie_pointer nya kosong
        client->cookie_pointer = http_get_header(client->headers_pointer, "Cookie");
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

        while(*cookie_pointer != '\0') cookie_pointer++;

        if (*++cookie_pointer == '\2') cookie_pointer++;
        if (*cookie_pointer == '\0' || *cookie_pointer == '\1') return NULL;
    }
}

char http_write(http_client* client, const char* data, size_t size) {
    if (send(client->socket, data, size, 0) <= 0) return -2;
    return 0;
}

char http_send_file(http_client* client, const char* name_file, char manual_code, char using_cache) {
    char temp_header[2048];

    #ifdef _WIN32
    HANDLE file_fd = CreateFile(name_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file_fd == INVALID_HANDLE_VALUE) return -1; // File not found
    #else
    int file_fd = open(name_file, O_RDONLY);
    struct stat file_stat;
    off_t offset = 0;
    ssize_t sent_bytes;
    if (file_fd == -1) return -1; // File not found
    if (fstat(file_fd, &file_stat) == -1) {
        close(file_fd);
        return -2; // Server internal error
    }
    #endif

    unsigned short header_len;
    if (manual_code) {
        memcpy(temp_header, "Connection: close\r\nContent-Type: ", 33);
        header_len = 33 + __private_mime_types(name_file, strlen(name_file), temp_header + 33);
        
        memcpy(temp_header + header_len, "\r\nContent-Length: ", 18);
        header_len += 18;
    
        #ifdef _WIN32
        header_len += sprintf(temp_header + header_len, "%ld", GetFileSize(file_fd, NULL));
        #else
        header_len += sprintf(temp_header + header_len, "%ld", file_stat.st_size);
        #endif
    
        if (using_cache) {
            memcpy(temp_header + header_len, "\r\nCache-Control: no-cache\r\nETag: ", 33);
            header_len += 33;
        } else {
            memcpy(temp_header + header_len, "\r\n\r\n", 4);
            header_len += 4;
        }
    } else {
        memcpy(temp_header, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: ", 50);
        header_len = 50 + __private_mime_types(name_file, strlen(name_file), temp_header + 50);
    
        memcpy(temp_header + header_len, "\r\nContent-Length: ", 18);
        header_len += 18;
    
        #ifdef _WIN32
        header_len += sprintf(temp_header + header_len, "%ld", GetFileSize(file_fd, NULL));
        #else
        header_len += sprintf(temp_header + header_len, "%ld", file_stat.st_size);
        #endif
    
        if (using_cache) {
            memcpy(temp_header + header_len, "\r\nCache-Control: no-cache\r\nETag: ", 33);
            header_len += 33;
        } else {
            memcpy(temp_header + header_len, "\r\n\r\n", 4);
            header_len += 4;
        }
    }

    if (using_cache) {
        uint64_t time_fmt;
        const char* client_date = http_get_header(client->headers_pointer, "If-None-Match");

        #ifdef _WIN32
        FILETIME last_modified;
        GetFileTime(file_fd, NULL, NULL, &last_modified);
        SYSTEMTIME sys_time;
        FileTimeToSystemTime(&last_modified, &sys_time);

        time_fmt = ((uint64_t)('0' + (sys_time.wMinute / 10))) |
                   ((uint64_t)('0' + (sys_time.wMinute % 10)) << 8) |
                   ((uint64_t)('0' + (sys_time.wSecond / 10)) << 16) |
                   ((uint64_t)('0' + (sys_time.wSecond % 10)) << 24) |
                   ((uint64_t)('\r') << 32) |
                   ((uint64_t)('\n') << 40) |
                   ((uint64_t)('\r') << 48) |
                   ((uint64_t)('\n') << 56);
        #else
        struct tm timeinfo;
        localtime_r(&file_stat.st_mtime, &timeinfo);
        
        time_fmt = ((uint64_t)('0' + (timeinfo.tm_min / 10))) |
                   ((uint64_t)('0' + (timeinfo.tm_min % 10)) << 8) |
                   ((uint64_t)('0' + (timeinfo.tm_sec / 10)) << 16) |
                   ((uint64_t)('0' + (timeinfo.tm_sec % 10)) << 24) |
                   ((uint64_t)('\r') << 32) |
                   ((uint64_t)('\n') << 40) |
                   ((uint64_t)('\r') << 48) |
                   ((uint64_t)('\n') << 56);
        #endif

        #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        time_fmt = __builtin_bswap64(time_fmt);
        #endif

        *(uint64_t*)(temp_header + header_len) = time_fmt;

        // WARNING: INI BELUM DI COBA DI BIG ENDIAN!
        if (client_date && *(uint32_t*)&time_fmt == *(uint32_t*)client_date) {
            http_write_string(client, "HTTP/1.1 304 Not Modified\r\n\r\n");
            close(file_fd);
            return 0;
        }
    }
    
    http_write(client, temp_header, header_len + 8);

    #ifdef _WIN32
    if (!TransmitFile(client->socket, file_fd, 0, 0, NULL, NULL, 0)) {
        printf("TransmitFile failed. Error: %d\n", WSAGetLastError());
        close(file_fd);
        return -2; // Server internal error
    }
    #else
    while (offset < file_stat.st_size) {
        if (sendfile(client->socket, file_fd, &offset, file_stat.st_size - offset) == -1) {
            close(file_fd);
            return -2; // Server internal error
        }
    }
    #endif

    close(file_fd);
    return 0; // OK
}

http* http_init_socket(const char* ip, unsigned short port, size_t max_sockets, http_callback callback) {
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    SOCKET r_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (r_socket == -1) {
        printf("Socket creation failed");
        return NULL;
    }

    // reuse port
    int optval = 1;
    setsockopt(r_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&optval, sizeof(optval));

    // Setup the TCP listening socket
    if (bind(r_socket, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_in)) == -1) {
        printf("bind error!\n");
        close(r_socket);
        return NULL;
    }

    // listen socket
    if (listen(r_socket, max_sockets) == -1) {
        printf("listen error!\n");
        close(r_socket);
        return NULL;
    }

    // non-blocking socket
    if (fcntl(r_socket, F_SETFL, fcntl(r_socket, F_GETFL, 0) | O_NONBLOCK) == -1) {
        printf("fcntl failed\n");
        close(r_socket);
        return NULL;
    }

    http* result = (http*)malloc(sizeof(http));
    memset(result, 0, sizeof(http));
    result->server_socket = r_socket;
    result->max_sockets = max_sockets;
    result->thread_rooms = (http_room*)malloc(max_sockets * sizeof(http_room));

    for (size_t a = 0; a < max_sockets; a++) {
        result->thread_rooms[a].client = (http_client*)malloc(sizeof(http_client));
        memset(result->thread_rooms[a].client, 0, sizeof(http_client));
        buffer_init(&result->thread_rooms[a].req_raw_data);
        result->thread_rooms[a].callback = callback;
    }
    result->still_on = 1;

    return result;
}

void* http_post_send_handle(void* args) {
    http_room* room = args;
    
    room->callback(room->client);

    clean_room(room);
    pthread_exit(NULL);
}

void http_start(http* http) {
    SOCKET epoll_fd = epoll_create1(0);

    int client_fd, nfds;
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = http->server_socket;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, http->server_socket, &event);

    struct epoll_event* client_events;
    client_events = (struct epoll_event*)malloc(http->max_sockets * sizeof(struct epoll_event));

    while(http->still_on) {
        nfds = epoll_wait(epoll_fd, client_events, http->max_sockets, -1);
        for (int a = 0; a < nfds; a++) {
            if (client_events[a].data.fd == http->server_socket) {
                while ((client_fd = accept(http->server_socket, NULL, NULL)) != -1) {
                    for (int a = 0; a < http->max_sockets; a++) {
                        if (http->thread_rooms[a].client->socket == 0) {
                            // set socket client menjadi non-blocking
                            fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK);
                            http->thread_rooms[a].client->socket = client_fd;

                            // untuk client fd
                            event.events = EPOLLIN;
                            event.data.ptr = &http->thread_rooms[a];
                            epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event);
                            break;
                        }
                    }
                }
            } else {
                if (client_events[a].events & EPOLLIN) {
                    http_room* room = (http_room*)client_events[a].data.ptr;

                    char data[HTTP_TEMPORARY_MAX_BUFFER]; // temporary data, dan di masukkan ke req_raw_data menggunakan buffer_append_n().
                    size_t total_recv = recv(room->client->socket, data, 1024, 0);

                    if (total_recv > 0) {
                        buffer_append_n(&room->req_raw_data, data, total_recv);
                        size_t remaining_output;

                        if (!room->no_need_check_request && cmp_stream(&room->status, data, total_recv, "\r\n\r\n", 4, &remaining_output) == 2) {
                            // WAKTUNYA PARSING HTTP!

                            room->no_need_check_request = 1;
                            char* cur_pos = room->req_raw_data.val;

                            // HTTP Method Parsing
                            room->client->method.val = cur_pos;
                            cur_pos = strchr(cur_pos, ' ');
                            if (cur_pos == 0) {
                                http_write_string(room->client, "HTTP/1.1 400 Bad Request\r\n\r\n");
                                clean_room(room);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, room->client->socket, NULL);
                                continue;
                            }
                            room->client->method.len = cur_pos - room->client->method.val;
                            
                            *cur_pos++ = 0;

                            // HTTP Path Parsing
                            room->client->path.val = cur_pos;
                            cur_pos = strchr(cur_pos, ' ');
                            
                            if (cur_pos == 0) {
                                http_write_string(room->client, "HTTP/1.1 400 Bad Request\r\n\r\n");
                                clean_room(room);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, room->client->socket, NULL);
                                continue;
                            }

                            *cur_pos = 0;

                            // Query parsing
                            room->client->query_pointer = strchr(room->client->path.val, '?');

                            if (room->client->query_pointer) {
                                room->client->path.len = room->client->query_pointer - room->client->path.val;
                                *room->client->query_pointer++ = 0;

                                char *param = room->client->query_pointer;
                                while (param && (param = strchr(param, '&'))) *param++ = 0;
                            }
                            else room->client->path.len = cur_pos - room->client->path.val;

                            cur_pos++;

                            // HTTP Version
                            room->client->version.val = cur_pos;

                            cur_pos = strchr(cur_pos, '\r');
                            if (cur_pos == 0) {
                                http_write_string(room->client, "HTTP/1.1 400 Bad Request\r\n\r\n");
                                clean_room(room);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, room->client->socket, NULL);
                                continue;
                            }

                            *(short*)cur_pos = 0;
                            room->client->version.len = cur_pos - room->client->version.val;
                            cur_pos += 2;

                            // HTTP Headers
                            room->client->cookie_pointer = NULL;
                            room->client->headers_pointer = cur_pos;

                            HEADER_PARSING:
                            cur_pos = strchr(cur_pos, '\r');
                            if (cur_pos == NULL) {
                                http_write_string(room->client, "HTTP/1.1 400 Bad Request\r\n\r\n");
                                clean_room(room);
                                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, room->client->socket, NULL);
                                continue;
                            }
                            if (*(int*)cur_pos == 0x0a0d0a0d) *(int*)cur_pos = 0, cur_pos += 4;
                            else {
                                *(short*)cur_pos = 0, cur_pos += 2;
                                goto HEADER_PARSING;
                            }

                            // HTTP Body Data
                            room->client->body.val = cur_pos;
                            room->client->body.len = total_recv - (cur_pos - room->client->method.val);

                            const char* c_len = http_get_header(room->client->headers_pointer, "content-length");
                            if (c_len) room->body_len_remaining = strtoull(c_len, NULL, 10);

                            if (room->client->body.len >= room->body_len_remaining) {
                                event.events = EPOLLOUT | EPOLLONESHOT;
                                event.data.ptr = client_events[a].data.ptr;
                                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, room->client->socket, &event);
                            }
                        }
                    }
                    else if (total_recv < 0) {
                        // ngecek data dulu ngab
                        if (room->client->body.len >= room->body_len_remaining) {
                            event.events = EPOLLOUT | EPOLLONESHOT;
                            event.data.ptr = client_events[a].data.ptr;
                            epoll_ctl(epoll_fd, EPOLL_CTL_MOD, room->client->socket, &event);
                        }
                    }
                    else { // jika client disconnect.
                        close(room->client->socket);
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, room->client->socket, NULL);
                        continue;
                    }
                    
                } else if (client_events[a].events & EPOLLOUT) {
                    http_room* room = (http_room*)client_events[a].data.ptr;
                    pthread_create(&room->thread_id, NULL, http_post_send_handle, (void*)room);
                }
            }
        }
    }
}
#endif
