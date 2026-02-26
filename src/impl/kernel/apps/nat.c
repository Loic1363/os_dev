#include "apps/nat.h"
#include "console.h"
#include "print.h"
#include "x86_64/pit.h"
#include "lib/x86_64/functions/ramfs.h"
#include "lib/x86_64/functions/string.h"

#define NAT_BUF_MAX 2048
#define NAT_PATH_MAX 64
#define NAT_STATUS_ROW 0
#define NAT_TEXT_ROW_START 1
#define NAT_CURSOR_BLINK_TICKS 25

static uint8_t g_nat_active = 0;
static char g_nat_path[NAT_PATH_MAX];
static char g_nat_buf[NAT_BUF_MAX];
static size_t g_nat_len = 0;
static size_t g_nat_cursor = 0;
static size_t g_nat_view_line = 0;
static uint8_t g_nat_dirty = 0;
static uint8_t g_nat_quit_armed = 0;
static char g_nat_status_msg[64];
static uint64_t g_nat_last_blink_tick = 0;
static uint8_t g_nat_cursor_visible = 1;
static size_t g_nat_cursor_row = NAT_TEXT_ROW_START;
static size_t g_nat_cursor_col = 0;
static char g_nat_clipboard[NAT_BUF_MAX];
static size_t g_nat_clipboard_len = 0;

static size_t nat_cols() {
    return print_get_cols();
}

static size_t nat_rows() {
    return print_get_rows();
}

static size_t nat_help_row() {
    size_t rows = nat_rows();
    return (rows >= 2) ? (rows - 2) : 0;
}

static size_t nat_msg_row() {
    size_t rows = nat_rows();
    return (rows >= 1) ? (rows - 1) : 0;
}

static size_t nat_text_row_end() {
    size_t help_row = nat_help_row();
    if (help_row <= NAT_TEXT_ROW_START) {
        return NAT_TEXT_ROW_START;
    }
    return help_row - 1;
}

static size_t nat_text_rows() {
    return nat_text_row_end() - NAT_TEXT_ROW_START + 1;
}

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
    } else if (cursor_line >= g_nat_view_line + nat_text_rows()) {
        g_nat_view_line = cursor_line - nat_text_rows() + 1;
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
    size_t row = nat_help_row();
    print_clear_row_at(row, PRINT_COLOR_BLACK, PRINT_COLOR_LIGHT_GRAY);
    print_write_str_at(row, 0, " ^O WriteOut  ^X Exit  ^K CutLn  ^U Uncut  ^A Home  ^E End  ^L Refresh ", PRINT_COLOR_BLACK, PRINT_COLOR_LIGHT_GRAY);
}

static void nat_render_status_msg() {
    size_t row = nat_msg_row();
    print_clear_row_at(row, PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
    print_write_str_at(row, 0, g_nat_status_msg, PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
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

    for (size_t r = NAT_TEXT_ROW_START; r <= nat_text_row_end(); r++) {
        print_clear_row_at(r, PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);

        size_t c = 0;
        while (target_start_idx < g_nat_len && c < nat_cols()) {
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
    if (cursor_line >= g_nat_view_line + nat_text_rows()) {
        cursor_line = g_nat_view_line + nat_text_rows() - 1;
    }
    if (cursor_col >= nat_cols()) {
        cursor_col = nat_cols() - 1;
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
    g_nat_quit_armed = 0;
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
    g_nat_quit_armed = 0;
}

static void nat_delete_range(size_t start, size_t end) {
    if (start >= end || end > g_nat_len) {
        return;
    }

    size_t count = end - start;
    for (size_t i = start; i + count < g_nat_len; i++) {
        g_nat_buf[i] = g_nat_buf[i + count];
    }
    g_nat_len -= count;
    g_nat_buf[g_nat_len] = '\0';

    if (g_nat_cursor > end) {
        g_nat_cursor -= count;
    } else if (g_nat_cursor > start) {
        g_nat_cursor = start;
    }
}

static void nat_insert_buffer(const char* data, size_t len) {
    if (len == 0) {
        return;
    }
    if (g_nat_len + len >= NAT_BUF_MAX) {
        nat_set_status("Paste too large");
        return;
    }

    for (size_t i = g_nat_len; i > g_nat_cursor; i--) {
        g_nat_buf[i + len - 1] = g_nat_buf[i - 1];
    }
    for (size_t i = 0; i < len; i++) {
        g_nat_buf[g_nat_cursor + i] = data[i];
    }
    g_nat_len += len;
    g_nat_cursor += len;
    g_nat_buf[g_nat_len] = '\0';
    g_nat_dirty = 1;
    g_nat_quit_armed = 0;
}

static void nat_get_current_line_bounds(size_t* out_start, size_t* out_end_exclusive) {
    size_t start = nat_line_start_from_index(g_nat_cursor);
    size_t end = nat_line_end_from_index(g_nat_cursor);
    if (end < g_nat_len && g_nat_buf[end] == '\n') {
        end++;
    }
    *out_start = start;
    *out_end_exclusive = end;
}

static void nat_cut_current_line() {
    size_t start;
    size_t end;
    nat_get_current_line_bounds(&start, &end);
    if (start == end) {
        g_nat_clipboard_len = 0;
        g_nat_clipboard[0] = '\0';
        nat_set_status("Nothing to cut");
        return;
    }

    size_t len = end - start;
    if (len >= NAT_BUF_MAX) {
        len = NAT_BUF_MAX - 1;
    }
    for (size_t i = 0; i < len; i++) {
        g_nat_clipboard[i] = g_nat_buf[start + i];
    }
    g_nat_clipboard[len] = '\0';
    g_nat_clipboard_len = len;

    nat_delete_range(start, end);
    g_nat_cursor = start;
    g_nat_dirty = 1;
    g_nat_quit_armed = 0;
    nat_set_status("Cut line");
}

static void nat_uncut() {
    if (g_nat_clipboard_len == 0) {
        nat_set_status("Clipboard empty");
        return;
    }
    nat_insert_buffer(g_nat_clipboard, g_nat_clipboard_len);
    nat_set_status("Uncut");
}

static void nat_move_home() {
    g_nat_cursor = nat_line_start_from_index(g_nat_cursor);
}

static void nat_move_end() {
    g_nat_cursor = nat_line_end_from_index(g_nat_cursor);
}

static void nat_save() {
    if (ramfs_write_file(g_nat_path, g_nat_buf, g_nat_len)) {
        g_nat_dirty = 0;
        g_nat_quit_armed = 0;
        nat_set_status("Wrote file");
    } else {
        nat_set_status("Write failed");
    }
}

static void nat_try_quit() {
    if (g_nat_dirty && !g_nat_quit_armed) {
        g_nat_quit_armed = 1;
        nat_set_status("Unsaved changes. ^O to save, ^X again to quit");
        return;
    }

    g_nat_active = 0;
    g_nat_quit_armed = 0;
    console_clear_screen();
}

static void nat_show_cursor_info() {
    char msg[64];
    size_t p = 0;
    size_t line = nat_line_number_of_cursor() + 1;
    size_t col = nat_cursor_column() + 1;
    msg[0] = '\0';

    const char* a = "Ln ";
    for (size_t i = 0; a[i] && p + 1 < sizeof(msg); i++) msg[p++] = a[i];
    {
        char tmp[20];
        size_t n = 0;
        uint64_t v = line;
        if (v == 0) tmp[n++] = '0';
        while (v > 0 && n < sizeof(tmp)) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
        while (n > 0 && p + 1 < sizeof(msg)) msg[p++] = tmp[--n];
    }
    if (p + 2 < sizeof(msg)) { msg[p++] = ','; msg[p++] = ' '; }
    const char* b = "Col ";
    for (size_t i = 0; b[i] && p + 1 < sizeof(msg); i++) msg[p++] = b[i];
    {
        char tmp[20];
        size_t n = 0;
        uint64_t v = col;
        if (v == 0) tmp[n++] = '0';
        while (v > 0 && n < sizeof(tmp)) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
        while (n > 0 && p + 1 < sizeof(msg)) msg[p++] = tmp[--n];
    }
    msg[p] = '\0';
    nat_set_status(msg);
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
    g_nat_quit_armed = 0;
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
        nat_save();
        g_nat_cursor_visible = 1;
        g_nat_last_blink_tick = pit_ticks();
        nat_render();
        return;
    }

    if (key_lower == 'o') {
        nat_save();
        g_nat_cursor_visible = 1;
        g_nat_last_blink_tick = pit_ticks();
        nat_render();
        return;
    }

    if (key_lower == 'x' || key_lower == 'q') {
        nat_try_quit();
        if (g_nat_active) {
            g_nat_cursor_visible = 1;
            g_nat_last_blink_tick = pit_ticks();
            nat_render();
        }
        return;
    }

    if (key_lower == 'k') {
        nat_cut_current_line();
        g_nat_cursor_visible = 1;
        g_nat_last_blink_tick = pit_ticks();
        nat_render();
        return;
    }

    if (key_lower == 'u') {
        nat_uncut();
        g_nat_cursor_visible = 1;
        g_nat_last_blink_tick = pit_ticks();
        nat_render();
        return;
    }

    if (key_lower == 'a') {
        nat_move_home();
        g_nat_cursor_visible = 1;
        g_nat_last_blink_tick = pit_ticks();
        nat_render();
        return;
    }

    if (key_lower == 'e') {
        nat_move_end();
        g_nat_cursor_visible = 1;
        g_nat_last_blink_tick = pit_ticks();
        nat_render();
        return;
    }

    if (key_lower == 'l') {
        nat_set_status("Refreshed");
        g_nat_cursor_visible = 1;
        g_nat_last_blink_tick = pit_ticks();
        nat_render();
        return;
    }

    if (key_lower == 'g') {
        nat_set_status("PipOS NAT: ^O WriteOut, ^X Exit, ^K Cut, ^U Uncut");
        g_nat_cursor_visible = 1;
        g_nat_last_blink_tick = pit_ticks();
        nat_render();
        return;
    }

    if (key_lower == 'c') {
        nat_show_cursor_info();
        g_nat_cursor_visible = 1;
        g_nat_last_blink_tick = pit_ticks();
        nat_render();
        return;
    }

    if (key_lower == 'w') {
        nat_set_status("Search not implemented yet");
        g_nat_cursor_visible = 1;
        g_nat_last_blink_tick = pit_ticks();
        nat_render();
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
