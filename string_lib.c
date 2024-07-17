#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "string_lib.h"

#define DEFAULT_INIT_SIZE 512

#define ALLOC_CUR_REALLOC(str, temp_size) \
while (str->alloc_cur < (str->len + temp_size + 1)) { \
    str->alloc_cur += DEFAULT_INIT_SIZE; \
    str->val = realloc(str->val, str->alloc_cur); \
}

void string_init(string* str) {
    memset(str, 0, sizeof(string));
    str->val = malloc(DEFAULT_INIT_SIZE);
    str->alloc_cur = DEFAULT_INIT_SIZE;
}

void string_add_format(string* str, char* format, ...) {
    va_list args;
    va_start(args, format);

    size_t temp_size = vsnprintf(NULL, 0, format, args);
    ALLOC_CUR_REALLOC(str, temp_size);
    va_end(args);

    va_start(args, format);
    vsprintf(str->val + str->len, format, args);

    str->len += temp_size;
    va_end(args);
}

void string_add(string* str, const char* val) {
    size_t str_len = strlen(val);
    ALLOC_CUR_REALLOC(str, str_len);

    strcpy(str->val + str->len, val);
    str->len += str_len;
}

void string_add_n(string* str, const char* val, size_t len) {
    ALLOC_CUR_REALLOC(str, len);

    memcpy(str->val + str->len, val, len + 1);
    str->len += len;
    str->val[str->len] = '\0';
}

void string_begin(string* str) {
    *str->val = '\0';
    str->len = 0;
}

void string_finalize(string* str) {
    free(str->val);
    str->val = NULL;
    str->len = 0;
    str->alloc_cur = 0;
}