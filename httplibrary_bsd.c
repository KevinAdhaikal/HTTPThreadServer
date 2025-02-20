#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
#include "httplibrary.h"

#define strcmp_last(data, data_len, target) !strcmp(data + (data_len - (sizeof(target) - 1)), target)
#define clean_room(room) \
close(room->client.socket); \
memset(&room->client, 0, sizeof(http_client)); \
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

char* http_get_cookie(http_client client, const char* key) {
    char* cookie_pointer = client.cookie_pointer;
    if (cookie_pointer == NULL) { // kita parsing dulu jika cookie_pointer nya kosong
        client.cookie_pointer = http_get_header(client.headers_pointer, "Cookie");
        cookie_pointer = client.cookie_pointer;
        if (cookie_pointer == NULL) return NULL;

        while(*cookie_pointer != '\0' && *cookie_pointer != '\1') {
            if (*cookie_pointer == ';') {
                *cookie_pointer++ = '\0';
                if (*cookie_pointer == ' ') *cookie_pointer++ = '\2';
            } else cookie_pointer++;
        }
        cookie_pointer = client.cookie_pointer;
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

char http_write(http_client client, const char* data, size_t size) {
    if (send(client.socket, data, size, 0) <= 0) return -2;
    return 0;
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
    memset(result->thread_rooms, 0, max_sockets * sizeof(http_room));
    for (size_t a = 0; a < max_sockets; a++) {
        buffer_init(&result->thread_rooms[a].req_raw_data);
        result->thread_rooms[a].callback = callback;
    }
    result->still_on = 1;

    return result;
}

static void* http_post_send_handle(void* args) {
    http_room* room = args;
    
    room->callback(room->client);
    
    clean_room(room);
    return NULL;
}

void http_start(http* http) {
    int kqueue_fd = kqueue();

    int client_fd, nfds;
    struct kevent event_set;
    struct kevent* event_clients = (struct kevent*)malloc(1024 * sizeof(struct kevent));

    EV_SET(&event_set, http->server_socket, EVFILT_READ, EV_ADD, 0, 0, NULL);
    kevent(kqueue_fd, &event_set, 1, NULL, 0, NULL);

    while(1) {
        nfds = kevent(kqueue_fd, NULL, 0, event_clients, 1024, NULL);
        for (int a = 0; a < nfds; a++) {
            int event_fd = event_clients[a].ident;
            if (event_fd == http->server_socket) {
                client_fd = accept(event_fd, NULL, NULL);
                for (int a = 0; a < http->max_sockets; a++) {
                    if (http->thread_rooms[a].client.socket == 0) {
                        // set socket client menjadi non-blocking
                        fcntl(client_fd, F_SETFL, fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK);
                        http->thread_rooms[a].client.socket = client_fd;

                        // untuk client fd
                        EV_SET(&event_set, client_fd, EVFILT_READ, EV_ADD, 0, 0, &http->thread_rooms[a]);
                        kevent(kqueue_fd, &event_set, 1, NULL, 0, NULL);
                        break;
                    }
                }
            }
            else {
                if (event_clients[a].filter == EVFILT_READ) {
                    http_room* room = (http_room*)event_clients[a].udata;

                    char data[HTTP_TEMPORARY_MAX_BUFFER]; // temporary data, dan di masukkan ke req_raw_data menggunakan buffer_append_n().
                    size_t total_recv = recv(event_fd, data, 1024, 0);

                    if (total_recv > 0) {
                        buffer_append_n(&room->req_raw_data, data, total_recv);
                        size_t remaining_output;

                        if (!room->no_need_check_request && cmp_stream(&room->status, data, total_recv, "\r\n\r\n", 4, &remaining_output) == 2) {
                            // WAKTUNYA PARSING HTTP!
                            room->no_need_check_request = 1;
                            char* cur_pos = room->req_raw_data.val;

                            // HTTP Method Parsing
                            room->client.method.val = cur_pos;
                            cur_pos = strchr(cur_pos, ' ');
                            if (cur_pos == 0) {
                                http_write_string(room->client, "HTTP/1.1 400 Bad Request\r\n\r\n");
                                clean_room(room);
                                EV_SET(&event_set, event_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                                kevent(kqueue_fd, &event_set, 1, NULL, 0, NULL);
                                continue;
                            }
                            room->client.method.len = cur_pos - room->client.method.val;
                            
                            *cur_pos++ = 0;

                            // HTTP Path Parsing
                            room->client.path.val = cur_pos;
                            cur_pos = strchr(cur_pos, ' ');
                            
                            if (cur_pos == 0) {
                                http_write_string(room->client, "HTTP/1.1 400 Bad Request\r\n\r\n");
                                clean_room(room);
                                EV_SET(&event_set, event_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                                kevent(kqueue_fd, &event_set, 1, NULL, 0, NULL);
                                continue;
                            }

                            *cur_pos = 0;

                            // Query parsing
                            room->client.query_pointer = strchr(room->client.path.val, '?');

                            if (room->client.query_pointer) {
                                room->client.path.len = room->client.query_pointer - room->client.path.val;
                                *room->client.query_pointer++ = 0;

                                char *param = room->client.query_pointer;
                                while (param && (param = strchr(param, '&'))) *param++ = 0;
                            }
                            else room->client.path.len = cur_pos - room->client.path.val;

                            cur_pos++;

                            // HTTP Version
                            room->client.version.val = cur_pos;

                            cur_pos = strchr(cur_pos, '\r');
                            if (cur_pos == 0) {
                                http_write_string(room->client, "HTTP/1.1 400 Bad Request\r\n\r\n");
                                clean_room(room);
                                EV_SET(&event_set, event_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                                kevent(kqueue_fd, &event_set, 1, NULL, 0, NULL);
                                continue;
                            }

                            *(short*)cur_pos = 0;
                            room->client.version.len = cur_pos - room->client.version.val;
                            cur_pos += 2;

                            // HTTP Headers
                            room->client.cookie_pointer = NULL;
                            room->client.headers_pointer = cur_pos;

                            HEADER_PARSING:
                            cur_pos = strchr(cur_pos, '\r');
                            if (cur_pos == NULL) {
                                http_write_string(room->client, "HTTP/1.1 400 Bad Request\r\n\r\n");
                                clean_room(room);
                                EV_SET(&event_set, event_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                                kevent(kqueue_fd, &event_set, 1, NULL, 0, NULL);
                                continue;
                            }
                            if (*(int*)cur_pos == 0x0a0d0a0d) *(int*)cur_pos = 0, cur_pos += 4;
                            else {
                                *(short*)cur_pos = 0, cur_pos += 2;
                                goto HEADER_PARSING;
                            }

                            // HTTP Body Data
                            room->client.body.val = cur_pos;
                            room->client.body.len = total_recv - (cur_pos - room->client.method.val);

                            const char* c_len = http_get_header(room->client.headers_pointer, "content-length");
                            if (c_len) room->body_len_remaining = strtoull(c_len, NULL, 10);

                            if (room->client.body.len >= room->body_len_remaining) {
                                // delete read dulu
                                EV_SET(&event_set, event_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                                kevent(kqueue_fd, &event_set, 1, NULL, 0, NULL);

                                // setelah delete read nya, baru add write.
                                EV_SET(&event_set, event_fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, &http->thread_rooms[a]);
                                kevent(kqueue_fd, &event_set, 1, NULL, 0, NULL);
                            }
                        }
                    }
                    else if (total_recv < 0) {
                        // ngecek data dulu ngab
                        if (room->client.body.len >= room->body_len_remaining) {
                            // delete read dulu
                            EV_SET(&event_set, event_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                            kevent(kqueue_fd, &event_set, 1, NULL, 0, NULL);

                            // setelah delete read nya, baru add write.
                            EV_SET(&event_set, event_fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, &http->thread_rooms[a]);
                            kevent(kqueue_fd, &event_set, 1, NULL, 0, NULL);
                        }
                    }
                    else { // jika client disconnect.
                        close(room->client.socket);
                        EV_SET(&event_set, event_fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
                        kevent(kqueue_fd, &event_set, 1, NULL, 0, NULL);
                        continue;
                    }
                }
                else if (event_clients[a].filter == EVFILT_WRITE) {
                    http_room* room = (http_room*)event_clients[a].udata;
                    pthread_create(&room->thread_id, NULL, http_post_send_handle, (void*)room);
                    pthread_detach(room->thread_id);
                }
            }
        }
    }
}
#endif