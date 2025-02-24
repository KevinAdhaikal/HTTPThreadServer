#ifndef BUFFER_LIB_HEADER
#define BUFFER_LIB_HEADER

#include <stdio.h>
#include <string.h>
#include <stdint.h> 

typedef struct {
    char* val;
    size_t len;
    size_t alloc_cur;
} buffer;

void buffer_init(buffer* str);
void buffer_string_append(buffer* str, const char* val);
void buffer_append_n(buffer* str, const char* val, size_t len);
void buffer_append_char(buffer* str, const char val);
void buffer_begin(buffer* str);
void buffer_begin_reinit(buffer* str);
void buffer_finalize(buffer* str);

#endif //BUFFER_LIB_HEADER