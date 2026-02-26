#include "apps/nat.h"
#include "console.h"
#include "print.h"
#include "x86_64/pit.h"
#include "lib/x86_64/functions/ramfs.h"
#include "lib/x86_64/functions/string.h"

#define NAT_BUF_MAX 2048
#define NAT_PATH_MAX 64
#define NAT_ROWS 25
#define NAT_COLS 80
#define NAT_TEXT_ROW_START 1
#define NAT_TEXT_ROW_END 22
#define NAT_TEXT_ROWS (NAT_TEXT_ROW_END - NAT_TEXT_ROW_START + 1)
#define NAT_STATUS_ROW 0
#define NAT_HELP_ROW 23
#define NAT_MSG_ROW 24
#define NAT_CURSOR_BLINK_TICKS 25

static uint8_t g_nat_active = 0;
static char g_nat_path[NAT_PATH_MAX];
static char g_nat_buf[NAT_BUF_MAX];
static size_t g_nat_len = 0;
static size_t g_nat_cursor = 0;
static size_t g_nat_view_line = 0;
static uint8_t g_nat_dirty = 0;
static char g_nat_status_msg[64];
static uint64_t g_nat_last_blink_tick = 0;
static uint8_t g_nat_cursor_visible = 1;
static size_t g_nat_cursor_row = NAT_TEXT_ROW_START;
static size_t g_nat_cursor_col = 0;

static void nat_set_status(const char* msg) {
    fn_strcopy(g_nat_status_msg, msg, sizeof(g_nat_status_msg));
}

static void nat_recompute_view_for_cursor();
static void nat_render();

static size_t nat_line_start_from_index(size_t index) {
    if (index > g_nat_len) {
        index = g_nat_len;
    }
    while (index > 0 && g_nat_buf[index - 1] != '\n') {
        index--;
    }
    return index;
}

static size_t nat_line_end_from_index(size_t index) {
    if (index > g_nat_len) {
        index = g_nat_len;
    }
    while (index < g_nat_len && g_nat_buf[index] != '\n') {
        index++;
    }
    return index;
}

static size_t nat_line_number_of_cursor() {
    size_t line = 0;
    for (size_t i = 0; i < g_nat_cursor && i < g_nat_len; i++) {
        if (g_nat_buf[i] == '\n') {
            line++;
        }
    }
    return line;
}

static size_t nat_cursor_column() {
    size_t start = nat_line_start_from_index(g_nat_cursor);
    return g_nat_cursor - start;
}

static size_t nat_index_from_line_and_col(size_t target_line, size_t target_col) {
    size_t line = 0;
    size_t i = 0;

    while (i < g_nat_len && line < target_line) {
        if (g_nat_buf[i] == '\n') {
            line++;
        }
        i++;
    }

    size_t line_start = i;
    size_t line_end = nat_line_end_from_index(line_start);
    size_t line_len = line_end - line_start;
    if (target_col > line_len) {
        target_col = line_len;
    }
    return line_start + target_col;
}

static void nat_recompute_view_for_cursor() {
    size_t cursor_line = nat_line_number_of_cursor();
    if (cursor_line < g_nat_view_line) {
        g_nat_view_line = cursor_line;
    } else if (cursor_line >= g_nat_view_line + NAT_TEXT_ROWS) {
        g_nat_view_line = cursor_line - NAT_TEXT_ROWS + 1;
    }
}

static void nat_render_header() {
    char line[80];
    size_t pos = 0;
    line[0] = '\0';

    const char* left = " PipOS NAT ";
    for (size_t i = 0; left[i] != '\0' && pos + 1 < sizeof(line); i++) {
        line[pos++] = left[i];
    }
    if (g_nat_dirty && pos + 2 < sizeof(line)) {
        line[pos++] = '*';
        line[pos++] = ' ';
    }
    for (size_t i = 0; g_nat_path[i] != '\0' && pos + 1 < sizeof(line); i++) {
        line[pos++] = g_nat_path[i];
    }
    line[pos] = '\0';

    print_clear_row_at(NAT_STATUS_ROW, PRINT_COLOR_BLACK, PRINT_COLOR_LIGHT_GREEN);
    print_write_str_at(NAT_STATUS_ROW, 0, line, PRINT_COLOR_BLACK, PRINT_COLOR_LIGHT_GREEN);
}

static void nat_render_help() {
    print_clear_row_at(NAT_HELP_ROW, PRINT_COLOR_BLACK, PRINT_COLOR_LIGHT_GRAY);
    print_write_str_at(NAT_HELP_ROW, 0, " Ctrl+S Save | Ctrl+Q Quit | Arrows Move | Tab=4 spaces ", PRINT_COLOR_BLACK, PRINT_COLOR_LIGHT_GRAY);
}

static void nat_render_status_msg() {
    print_clear_row_at(NAT_MSG_ROW, PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
    print_write_str_at(NAT_MSG_ROW, 0, g_nat_status_msg, PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
}

static void nat_render_text() {
    size_t line_no = 0;
    size_t idx = 0;
    size_t target_start_idx = 0;

    while (idx < g_nat_len && line_no < g_nat_view_line) {
        if (g_nat_buf[idx] == '\n') {
            line_no++;
        }
        idx++;
    }
    target_start_idx = idx;

    for (size_t r = NAT_TEXT_ROW_START; r <= NAT_TEXT_ROW_END; r++) {
        print_clear_row_at(r, PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);

        size_t c = 0;
        while (target_start_idx < g_nat_len && c < NAT_COLS) {
            char ch = g_nat_buf[target_start_idx];
            if (ch == '\n') {
                target_start_idx++;
                break;
            }
            print_write_char_at(r, c, ch, PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
            target_start_idx++;
            c++;
        }

        while (target_start_idx < g_nat_len && g_nat_buf[target_start_idx] == '\n' && c == 0) {
            target_start_idx++;
            break;
        }
    }
}

static void nat_update_cursor_screen_position() {
    size_t cursor_line = nat_line_number_of_cursor();
    size_t cursor_col = nat_cursor_column();

    if (cursor_line < g_nat_view_line) {
        cursor_line = g_nat_view_line;
    }
    if (cursor_line >= g_nat_view_line + NAT_TEXT_ROWS) {
        cursor_line = g_nat_view_line + NAT_TEXT_ROWS - 1;
    }
    if (cursor_col >= NAT_COLS) {
        cursor_col = NAT_COLS - 1;
    }

    g_nat_cursor_row = NAT_TEXT_ROW_START + (cursor_line - g_nat_view_line);
    g_nat_cursor_col = cursor_col;
}

static void nat_draw_cursor_overlay() {
    nat_update_cursor_screen_position();
    if (!g_nat_cursor_visible) {
        return;
    }
    print_write_char_at(g_nat_cursor_row, g_nat_cursor_col, '_', PRINT_COLOR_LIGHT_GREEN, PRINT_COLOR_BLACK);
}

static void nat_render() {
    nat_recompute_view_for_cursor();
    nat_render_header();
    nat_render_text();
    nat_render_help();
    nat_render_status_msg();
    nat_draw_cursor_overlay();
}

static void nat_insert_char(char ch) {
    if (g_nat_len + 1 >= NAT_BUF_MAX) {
        nat_set_status("Buffer full");
        return;
    }
    for (size_t i = g_nat_len; i > g_nat_cursor; i--) {
        g_nat_buf[i] = g_nat_buf[i - 1];
    }
    g_nat_buf[g_nat_cursor] = ch;
    g_nat_len++;
    g_nat_buf[g_nat_len] = '\0';
    g_nat_cursor++;
    g_nat_dirty = 1;
}

static void nat_delete_before_cursor() {
    if (g_nat_cursor == 0 || g_nat_len == 0) {
        return;
    }
    for (size_t i = g_nat_cursor - 1; i < g_nat_len - 1; i++) {
        g_nat_buf[i] = g_nat_buf[i + 1];
    }
    g_nat_len--;
    g_nat_cursor--;
    g_nat_buf[g_nat_len] = '\0';
    g_nat_dirty = 1;
}

uint8_t nat_is_active() {
    return g_nat_active;
}

uint8_t nat_open(const char* path) {
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    if (!ramfs_open_or_create_file(path)) {
        return 0;
    }

    fn_strcopy(g_nat_path, path, sizeof(g_nat_path));
    g_nat_len = 0;
    g_nat_cursor = 0;
    g_nat_view_line = 0;
    g_nat_dirty = 0;
    g_nat_cursor_visible = 1;
    g_nat_last_blink_tick = pit_ticks();
    nat_set_status("Opened");

    size_t read_len = 0;
    if (!ramfs_read_file(path, g_nat_buf, sizeof(g_nat_buf), &read_len)) {
        return 0;
    }
    g_nat_len = read_len;
    g_nat_cursor = g_nat_len;
    g_nat_active = 1;
    print_clear();
    nat_render();
    return 1;
}

void nat_handle_char(char ch) {
    if (!g_nat_active) return;
    nat_insert_char(ch);
    nat_set_status("Editing");
    g_nat_cursor_visible = 1;
    g_nat_last_blink_tick = pit_ticks();
    nat_render();
}

void nat_handle_backspace() {
    if (!g_nat_active) return;
    nat_delete_before_cursor();
    nat_set_status("Editing");
    g_nat_cursor_visible = 1;
    g_nat_last_blink_tick = pit_ticks();
    nat_render();
}

void nat_handle_enter() {
    nat_handle_char('\n');
}

void nat_handle_tab() {
    if (!g_nat_active) return;
    for (uint8_t i = 0; i < 4; i++) {
        nat_insert_char(' ');
    }
    nat_set_status("Editing");
    g_nat_cursor_visible = 1;
    g_nat_last_blink_tick = pit_ticks();
    nat_render();
}

void nat_handle_arrow_left() {
    if (!g_nat_active) return;
    if (g_nat_cursor > 0) g_nat_cursor--;
    g_nat_cursor_visible = 1;
    g_nat_last_blink_tick = pit_ticks();
    nat_render();
}

void nat_handle_arrow_right() {
    if (!g_nat_active) return;
    if (g_nat_cursor < g_nat_len) g_nat_cursor++;
    g_nat_cursor_visible = 1;
    g_nat_last_blink_tick = pit_ticks();
    nat_render();
}

void nat_handle_arrow_up() {
    if (!g_nat_active) return;
    size_t line = nat_line_number_of_cursor();
    size_t col = nat_cursor_column();
    if (line > 0) {
        g_nat_cursor = nat_index_from_line_and_col(line - 1, col);
    }
    g_nat_cursor_visible = 1;
    g_nat_last_blink_tick = pit_ticks();
    nat_render();
}

void nat_handle_arrow_down() {
    if (!g_nat_active) return;
    size_t line = nat_line_number_of_cursor();
    size_t col = nat_cursor_column();
    size_t next_line_start = nat_index_from_line_and_col(line + 1, 0);
    if (next_line_start != nat_index_from_line_and_col(line, 0) || line == 0) {
        if (next_line_start <= g_nat_len) {
            size_t cur_line_start = nat_index_from_line_and_col(line, 0);
            if (next_line_start != cur_line_start || (cur_line_start < g_nat_len && g_nat_buf[cur_line_start] == '\n')) {
                g_nat_cursor = nat_index_from_line_and_col(line + 1, col);
            }
        }
    }
    g_nat_cursor_visible = 1;
    g_nat_last_blink_tick = pit_ticks();
    nat_render();
}

void nat_handle_ctrl(uint8_t key_lower) {
    if (!g_nat_active) return;

    if (key_lower == 's') {
        if (ramfs_write_file(g_nat_path, g_nat_buf, g_nat_len)) {
            g_nat_dirty = 0;
            nat_set_status("Saved");
        } else {
            nat_set_status("Save failed");
        }
        g_nat_cursor_visible = 1;
        g_nat_last_blink_tick = pit_ticks();
        nat_render();
        return;
    }

    if (key_lower == 'q') {
        g_nat_active = 0;
        console_clear_screen();
        return;
    }
}

void nat_tick() {
    if (!g_nat_active) return;
    uint64_t ticks = pit_ticks();
    if ((ticks - g_nat_last_blink_tick) < NAT_CURSOR_BLINK_TICKS) {
        return;
    }
    g_nat_last_blink_tick = ticks;
    g_nat_cursor_visible = !g_nat_cursor_visible;
    nat_render();
}
