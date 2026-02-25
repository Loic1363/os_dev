#pragma once

#include <stddef.h>
#include <stdint.h>

uint8_t fn_streq(const char* a, const char* b);
uint8_t fn_strstarts(const char* str, const char* prefix);
size_t fn_strlen(const char* str);
void fn_strcopy(char* dst, const char* src, size_t max_len);
