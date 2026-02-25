#include "print.h"

const static size_t NUM_COLS = 80;
const static size_t NUM_ROWS = 25;
const static size_t CONSOLE_FIRST_ROW = 1; // reserve the first row for a status bar

struct Char {
    uint8_t character;
    uint8_t color;
};

struct Char* buffer = (struct Char*) 0xb8000;
size_t col = 0;
size_t row = 0;
uint8_t color = PRINT_COLOR_WHITE | PRINT_COLOR_BLACK << 4;

static uint8_t make_color(uint8_t foreground, uint8_t background) {
    return foreground + (background << 4);
}

void clear_row(size_t row) {
    struct Char empty = (struct Char) {
        .character = ' ',
        .color = color,
    };

    for (size_t col = 0; col < NUM_COLS; col++) {
        buffer[col + NUM_COLS * row] = empty;
    }
}

void print_clear() {
    for (size_t i = 0; i < NUM_ROWS; i++) {
        clear_row(i);
    }

    col = 0;
    row = CONSOLE_FIRST_ROW;
}

void print_newline() {
    col = 0;

    if (row < NUM_ROWS - 1) {
        row++;
        return;
    }

    for (size_t row_index = CONSOLE_FIRST_ROW + 1; row_index < NUM_ROWS; row_index++) {
        for (size_t col_index = 0; col_index < NUM_COLS; col_index++) {
            struct Char character = buffer[col_index + NUM_COLS * row_index];
            buffer[col_index + NUM_COLS * (row_index - 1)] = character;
        }
    }

    clear_row(NUM_ROWS - 1);
}

void print_backspace() {
    if (col == 0) {
        return;
    }

    col--;
    buffer[col + NUM_COLS * row] = (struct Char) {
        .character = ' ',
        .color = color,
    };
}

void print_clear_row_at(size_t row_index, uint8_t foreground, uint8_t background) {
    if (row_index >= NUM_ROWS) {
        return;
    }

    struct Char empty = (struct Char) {
        .character = ' ',
        .color = make_color(foreground, background),
    };

    for (size_t col_index = 0; col_index < NUM_COLS; col_index++) {
        buffer[col_index + NUM_COLS * row_index] = empty;
    }
}

void print_write_str_at(size_t row_index, size_t col_index, char* string, uint8_t foreground, uint8_t background) {
    if (row_index >= NUM_ROWS || col_index >= NUM_COLS) {
        return;
    }

    uint8_t local_color = make_color(foreground, background);

    for (size_t i = 0; string[i] != '\0'; i++) {
        if (col_index + i >= NUM_COLS) {
            return;
        }

        buffer[(col_index + i) + NUM_COLS * row_index] = (struct Char) {
            .character = (uint8_t) string[i],
            .color = local_color,
        };
    }
}

void print_char(char character) {
    if (character == '\n') {
        print_newline();
        return;
    }

    if (col >= NUM_COLS) {
        print_newline();
    }

    buffer[col + NUM_COLS * row] = (struct Char) {
        .character = (uint8_t) character,
        .color = color,
    };

    col++;
}

void print_str(char* str) {
    for (size_t i = 0; 1; i++) {
        char character = (uint8_t) str[i];

        if (character == '\0') {
            return;
        }

        print_char(character);
    }
}

void print_set_color(uint8_t foreground, uint8_t background) {
    color = make_color(foreground, background);
}

void print_uint64_dec(uint64_t value) {
    if (value == 0) {
        print_char('0');
        return;
    }

    char buffer[20];
    int i = 0;

    while (value > 0) {
        buffer[i++] = (value % 10) + '0';
        value /= 10;
    }

    while (i-- > 0) {
        print_char(buffer[i]);
    }
}

void print_uint64_hex(uint64_t value) {
    if (value == 0) {
        print_char('0');
        return;
    }

    char buffer[16];
    int i = 0;

    while (value > 0) {
        uint8_t digit = value & 0xF;

        if (digit < 10) {
            buffer[i++] = digit + '0';
        } else {
            buffer[i++] = digit - 10 + 'A';
        }

        value >>= 4;
    }

    while (i-- > 0) {
        print_char(buffer[i]);
    }
}

void print_uint64_bin(uint64_t value) {
    char buffer[64];

    for (size_t i = 0; i < 64; i++) {
        buffer[i] = (value & 1) + '0';
        value >>= 1;
    }

    for (size_t i = 64; i > 0; i--) {
        print_char(buffer[i - 1]);
    }
}
