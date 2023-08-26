#include <stdio.h>
#include <string.h>

#include "httplibrary.h"

void callback(http_event* e) {
    printf("%s: %s\n", e->headers.method, e->headers.path);
    http_write(e, "Hello World", 0);
}

int main() {
    http_start(http_init(8080), callback);
    return 0;
}