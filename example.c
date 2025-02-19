#include <stdio.h>
#include <string.h>

#include "httplibrary.h"

void http_handler(http_client* client) {
    http_write_string(client, "HTTP/1.1 200 OK\r\n\r\nHello World");
}

int main() {
    http_start(http_init_socket("127.0.0.1", 8080, 1024, http_handler));
    return 0;
}
