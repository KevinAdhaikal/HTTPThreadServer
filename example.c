#include <stdio.h>

#include "http1.h"

void client_process(http_event* e) {
    http_write(e, "hello world", 11);
}

int main() {
    http_start(http_init(80), client_process);
    return 0;
}
