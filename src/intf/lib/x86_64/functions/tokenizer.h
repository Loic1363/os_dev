#pragma once

#include <stddef.h>
#include <stdint.h>

void fn_skip_spaces(const char* s, size_t* pos);
uint8_t fn_next_token(const char* s, size_t* pos, char* out, size_t out_max);
const char* fn_rest_after_spaces(const char* s, size_t pos);
