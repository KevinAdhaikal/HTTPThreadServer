// TODO: Close socket yang idle

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <stdio.h>
#include <stdlib.h>

#include "buffer_lib.h"
#include "httplibrary.h"

#define PORT 8080
#define MAX_THREADS 256

WSADATA wsa_data;

#define strcmp_last(data, data_len, target) !strcmp(data + (data_len - (sizeof(target) - 1)), target)

static char __cmp_stream(cmp_stream_stat* status, char* data, size_t data_len, const char* target, size_t target_len) {
    size_t match = *status;

    // Jika ada pencocokan parsial sebelumnya, lanjutkan pencarian dari sana
    if (match > 0) {
        size_t remaining = target_len - match;
        if (data_len >= remaining && memcmp(data, target + match, remaining) == 0) {
            *status = 0;
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

char http_send_file(http_client client, const char* name_file, char manual_code, char using_cache) {
    char temp_header[2048];

    HANDLE file_fd = CreateFile(name_file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file_fd == INVALID_HANDLE_VALUE) return -1; // File not found

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

        header_len += sprintf(temp_header + header_len, "%ld", GetFileSize(file_fd, NULL));
    
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
        const char* client_date = http_get_header(client.headers_pointer, "If-None-Match");

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

        #if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        time_fmt = __builtin_bswap64(time_fmt);
        #endif

        *(uint64_t*)(temp_header + header_len) = time_fmt;

        // WARNING: INI BELUM DI COBA DI BIG ENDIAN!
        if (client_date && *(uint32_t*)&time_fmt == *(uint32_t*)client_date) {
            http_write_string(client, "HTTP/1.1 304 Not Modified\r\n\r\n");
            CloseHandle(file_fd);
            return 0;
        }
    }
    
    http_write(client, temp_header, header_len + 8);

    if (!TransmitFile(client.socket, file_fd, 0, 0, NULL, NULL, 0)) {
        printf("TransmitFile failed. Error: %d\n", WSAGetLastError());
        CloseHandle(file_fd);
        return -2; // Server internal error
    }

    CloseHandle(file_fd);
    return 0; // OK
}

static DWORD WINAPI client_handler(LPVOID lpParam) {
    HANDLE iocp = (HANDLE)lpParam;
    DWORD bytesTransferred;
    ULONG_PTR completionKey;
    LPOVERLAPPED overlapped;
    http_room* room;
    WSABUF wsa_buf;

    while (1) {
        BOOL result = GetQueuedCompletionStatus(iocp, &bytesTransferred, &completionKey, (LPOVERLAPPED*)&room, INFINITE);
        if (!result || bytesTransferred == 0) {
            closesocket(completionKey);
            buffer_finalize(&room->req_raw_data);
            free(room);
            continue;
        }

        buffer_append_n(&room->req_raw_data, room->temporary_data, bytesTransferred);
        if (__cmp_stream(&room->status, room->temporary_data, bytesTransferred, "\r\n\r\n", 4) == 2) {
            room->callback(room->client);
            closesocket(room->client.socket);
            buffer_finalize(&room->req_raw_data);
            free(room);
        } else {
            wsa_buf.buf = room->temporary_data;
            wsa_buf.len = HTTP_TEMPORARY_MAX_BUFFER;

            DWORD flags = 0, bytesReceived = 0;
            WSARecv(room->client.socket, &wsa_buf, 1, &bytesReceived, &flags, (OVERLAPPED *)room, NULL);
        }
    }
    return 0;
}

http* http_init_socket(const char* ip, unsigned short port, size_t max_threads, http_callback callback) {
    WSAStartup(MAKEWORD(2, 2), &wsa_data);

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
        closesocket(r_socket);
        return NULL;
    }

    // listen socket
    if (listen(r_socket, SOMAXCONN) == -1) {
        printf("listen error!\n");
        closesocket(r_socket);
        return NULL;
    }

    http* result = (http*)malloc(sizeof(http));
    memset(result, 0, sizeof(http));
    result->server_socket = r_socket;
    result->max_threads = max_threads;
    result->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, MAX_THREADS);
    result->still_on = 1;
    result->callback = callback;

    for (int i = 0; i < max_threads; i++) CreateThread(NULL, 0, client_handler, result->iocp, 0, NULL);

    return result;
}

void http_start(http* http) {
    WSABUF wsa_buf;

    while(http->still_on) {
        SOCKET client_socket = accept(http->server_socket, NULL, NULL);
        if (client_socket == INVALID_SOCKET) continue;

        http_room* room = (http_room*)malloc(sizeof(http_room));

        memset(room, 0, sizeof(http_room));

        buffer_init(&room->req_raw_data);
        room->client.socket = client_socket;
        room->callback = http->callback;
        wsa_buf.buf = room->temporary_data;
        wsa_buf.len = HTTP_TEMPORARY_MAX_BUFFER;
        
        CreateIoCompletionPort((HANDLE)client_socket, http->iocp, (ULONG_PTR)client_socket, 0);
        DWORD flags = 0, bytesReceived = 0;
        WSARecv(client_socket, &wsa_buf, 1, &bytesReceived, &flags, (OVERLAPPED*)room, NULL);
    }
}
#endif