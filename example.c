#include <stdio.h>
#include "httplibrary.h"

void http_handler(http_client* client) {
    send(client->socket, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\nHello World", 55, 0);
}

int main() {
    SOCKET s_socket = http_init_socket("0.0.0.0", 8080);
    http_start(s_socket, http_handler);

    http_close_socket(s_socket);

    return 0;
}