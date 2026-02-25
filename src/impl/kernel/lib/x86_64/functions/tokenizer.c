#include "lib/x86_64/functions/tokenizer.h"

void fn_skip_spaces(const char* s, size_t* pos) {
    while (s[*pos] == ' ') {
        (*pos)++;
    }
}

uint8_t fn_next_token(const char* s, size_t* pos, char* out, size_t out_max) {
    fn_skip_spaces(s, pos);
    if (s[*pos] == '\0') {
        if (out_max > 0) {
            out[0] = '\0';
        }
        return 0;
    }

    size_t i = 0;
    while (s[*pos] != '\0' && s[*pos] != ' ') {
        if (i + 1 < out_max) {
            out[i++] = s[*pos];
        }
        (*pos)++;
    }

    if (out_max > 0) {
        out[i] = '\0';
    }
    return 1;
}

const char* fn_rest_after_spaces(const char* s, size_t pos) {
    while (s[pos] == ' ') {
        pos++;
    }
    return s + pos;
}
