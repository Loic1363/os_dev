#include "lib/x86_64/functions/string.h"

uint8_t fn_streq(const char* a, const char* b) {
    for (size_t i = 0; ; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
        if (a[i] == '\0') {
            return 1;
        }
    }
}

uint8_t fn_strstarts(const char* str, const char* prefix) {
    for (size_t i = 0; ; i++) {
        if (prefix[i] == '\0') {
            return 1;
        }
        if (str[i] != prefix[i]) {
            return 0;
        }
    }
}

size_t fn_strlen(const char* str) {
    size_t n = 0;
    while (str[n] != '\0') {
        n++;
    }
    return n;
}

void fn_strcopy(char* dst, const char* src, size_t max_len) {
    size_t i = 0;
    if (max_len == 0) {
        return;
    }
    for (; i + 1 < max_len && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}
