#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "buffer_lib.h"

#define DEFAULT_INIT_SIZE 512

#define ALLOC_CUR_REALLOC(buf, temp_size) \
if (buf->alloc_cur <= (buf->len + temp_size)) { \
    while (buf->alloc_cur <= (buf->len + temp_size)) { \
        buf->alloc_cur += DEFAULT_INIT_SIZE; \
    } \
    buf->val = realloc(buf->val, buf->alloc_cur); \
}

void buffer_init(buffer* buf) {
    memset(buf, 0, sizeof(buffer));
    buf->val = malloc(DEFAULT_INIT_SIZE);
    buf->alloc_cur = DEFAULT_INIT_SIZE;
    *buf->val = 0;
}

void buffer_string_append(buffer* buf, const char* val) {
    size_t buf_len = strlen(val);
    ALLOC_CUR_REALLOC(buf, buf_len);

    memcpy(buf->val + buf->len, val, buf_len);
    buf->len += buf_len;
}

void buffer_append_n(buffer* buf, const char* val, size_t len) {
    ALLOC_CUR_REALLOC(buf, len);

    memcpy(buf->val + buf->len, val, len);
    buf->len += len;
}

void buffer_append_char(buffer* buf, const char val) {
    ALLOC_CUR_REALLOC(buf, 1);
    buf->val[buf->len++] = val;
}

void buffer_begin(buffer* buf) {
    *buf->val = 0;
    buf->len = 0;
}

void buffer_begin_reinit(buffer* buf) {
    if (buf->val) {
        free(buf->val);
        buf->val = malloc(DEFAULT_INIT_SIZE);
        *buf->val = 0;
        buf->len = 0;
        buf->alloc_cur = DEFAULT_INIT_SIZE;
    }
}

void buffer_finalize(buffer* buf) {
    if (buf->val) free(buf->val);
    buf->val = NULL;
    buf->len = 0;
    buf->alloc_cur = 0;
}