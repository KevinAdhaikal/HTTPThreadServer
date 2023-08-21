#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "httplibrary.h"

void event_handler(http_event *e) {
    printf("%s: %s\n", e->headers.method, e->headers.path);
    http_write(e, "Hello World", 0);
}

int main() {
    httpRun(httpInit(8080), event_handler);
    return 0;
}
