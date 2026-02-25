#include "bool.h"
#include "console.h"
#include "print.h"
#include "x86_64/pit.h"
#include "x86_64/rtc.h"
#include "lib/x86_64/functions/ramfs.h"
#include "lib/x86_64/functions/string.h"
#include "lib/x86_64/functions/tokenizer.h"

#define CONSOLE_LINE_MAX 64
#define CONSOLE_HISTORY_MAX 8
#define STATUS_ROW 0
#define STATUS_BUF_MAX 80
#define CONSOLE_PIT_HZ 100
#define CURSOR_BLINK_TICKS 25
#define CMD_TOKEN_MAX 32

static char g_console_line[CONSOLE_LINE_MAX];
static uint8_t g_console_line_len = 0;
static char g_console_history[CONSOLE_HISTORY_MAX][CONSOLE_LINE_MAX];
static uint8_t g_console_history_count = 0;
static uint8_t g_console_history_next_slot = 0;
static uint8_t g_console_history_nav = 0xFF;
static char g_console_history_scratch[CONSOLE_LINE_MAX];
static uint64_t g_last_status_ticks = (uint64_t) -1;
static uint8_t g_last_status_rtc_second = 255;
static uint64_t g_last_cursor_blink_tick = 0;
static uint8_t g_cursor_visible = 0;
static size_t g_cursor_row = 1;
static size_t g_cursor_col = 0;

static void buf_append_char(char* buf, size_t* len, char ch) {
    if (*len >= (STATUS_BUF_MAX - 1)) {
        return;
    }
    buf[*len] = ch;
    (*len)++;
    buf[*len] = '\0';
}

static void buf_append_str(char* buf, size_t* len, const char* str) {
    for (size_t i = 0; str[i] != '\0'; i++) {
        buf_append_char(buf, len, str[i]);
    }
}

static void buf_append_u64_dec(char* buf, size_t* len, uint64_t value) {
    if (value == 0) {
        buf_append_char(buf, len, '0');
        return;
    }

    char tmp[20];
    size_t n = 0;
    while (value > 0 && n < sizeof(tmp)) {
        tmp[n++] = (char) ('0' + (value % 10));
        value /= 10;
    }
    while (n > 0) {
        n--;
        buf_append_char(buf, len, tmp[n]);
    }
}

static void print_2digits(uint8_t value) {
    print_char((char) ('0' + ((value / 10) % 10)));
    print_char((char) ('0' + (value % 10)));
}

static void console_print_prompt() {
    print_set_color(PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
    print_str("\n> ");
}

static void console_cursor_hide() {
    if (!g_cursor_visible) {
        return;
    }
    print_write_char_at(g_cursor_row, g_cursor_col, ' ', PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
    g_cursor_visible = 0;
}

static void console_cursor_show() {
    size_t row_index;
    size_t col_index;
    print_get_cursor(&row_index, &col_index);
    g_cursor_row = row_index;
    g_cursor_col = col_index;
    print_write_char_at(g_cursor_row, g_cursor_col, '_', PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
    g_cursor_visible = 1;
}

static void console_cursor_refresh_after_output() {
    g_last_cursor_blink_tick = pit_ticks();
    console_cursor_show();
}

static void console_reset_line() {
    g_console_line_len = 0;
    g_console_line[0] = '\0';
}

static void console_print_uptime() {
    uint64_t ticks = pit_ticks();
    uint64_t seconds = ticks / CONSOLE_PIT_HZ;
    uint64_t centis = ticks % CONSOLE_PIT_HZ;

    print_str("uptime=");
    print_uint64_dec(seconds);
    print_char('.');
    print_char((char) ('0' + ((centis / 10) % 10)));
    print_char((char) ('0' + (centis % 10)));
    print_str("s\n");
}

static void console_print_time() {
    uint8_t h = rtc_hours();
    uint8_t m = rtc_minutes();
    uint8_t s = rtc_seconds();

    print_str("time=");
    print_2digits(h);
    print_char(':');
    print_2digits(m);
    print_char(':');
    print_2digits(s);
    print_char('\n');
}

static void console_print_about() {
    print_str("PipOS - hobby x86_64 kernel\n");
}

static void console_print_version() {
    print_str("version=0.1-dev\n");
}

static void console_trigger_panic_de() {
    asm volatile(
        "xor %%rdx, %%rdx\n"
        "mov $1, %%rax\n"
        "xor %%rcx, %%rcx\n"
        "div %%rcx\n"
        :
        :
        : "rax", "rcx", "rdx"
    );
}

static void console_trigger_panic_gp() {
    asm volatile(
        "xor %%eax, %%eax\n"
        "xor %%edx, %%edx\n"
        "mov $0xffffffff, %%ecx\n"
        "wrmsr\n"
        :
        :
        : "rax", "rcx", "rdx"
    );
}

static void console_trigger_panic_pf() {
    volatile uint64_t* ptr = (volatile uint64_t*) 0x0000400000000000ULL;
    volatile uint64_t value = *ptr;
    (void) value;
}

static void console_history_store_current() {
    if (g_console_line_len == 0) {
        return;
    }
    fn_strcopy(g_console_history[g_console_history_next_slot], g_console_line, CONSOLE_LINE_MAX);
    g_console_history_next_slot = (uint8_t) ((g_console_history_next_slot + 1) % CONSOLE_HISTORY_MAX);
    if (g_console_history_count < CONSOLE_HISTORY_MAX) {
        g_console_history_count++;
    }
}

static uint8_t console_history_slot_from_nav(uint8_t nav_index) {
    uint8_t newest_slot = (uint8_t) ((g_console_history_next_slot + CONSOLE_HISTORY_MAX - 1) % CONSOLE_HISTORY_MAX);
    return (uint8_t) ((newest_slot + CONSOLE_HISTORY_MAX - nav_index) % CONSOLE_HISTORY_MAX);
}

static void console_replace_current_line(const char* text) {
    console_cursor_hide();
    while (g_console_line_len > 0) {
        g_console_line_len--;
        g_console_line[g_console_line_len] = '\0';
        print_set_color(PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
        print_backspace();
    }
    for (size_t i = 0; text[i] != '\0'; i++) {
        if (g_console_line_len >= (CONSOLE_LINE_MAX - 1)) {
            break;
        }
        g_console_line[g_console_line_len++] = text[i];
        g_console_line[g_console_line_len] = '\0';
        print_char(text[i]);
    }
    console_cursor_refresh_after_output();
}

static void console_submit_line() {
    console_cursor_hide();
    g_console_line[g_console_line_len] = '\0';

    print_set_color(PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
    print_char('\n');

    if (g_console_line_len == 0) {
        console_print_prompt();
        console_cursor_refresh_after_output();
        return;
    }

    console_history_store_current();
    g_console_history_nav = 0xFF;

    char cmd[CMD_TOKEN_MAX];
    char arg1[CONSOLE_LINE_MAX];
    char arg2[CONSOLE_LINE_MAX];
    size_t pos = 0;
    fn_next_token(g_console_line, &pos, cmd, sizeof(cmd));
    size_t after_cmd_pos = pos;
    uint8_t has_arg1 = fn_next_token(g_console_line, &pos, arg1, sizeof(arg1));
    uint8_t has_arg2 = fn_next_token(g_console_line, &pos, arg2, sizeof(arg2));

    if (fn_streq(cmd, "help")) {
        print_str("Commands: help clear cls ticks time uptime echo about version panic_de panic_gp panic_pf pwd ls tree cd mkdir touch mv\n");
    } else if (fn_streq(cmd, "clear") || fn_streq(cmd, "cls")) {
        print_clear();
        console_render_status_line();
    } else if (fn_streq(cmd, "ticks")) {
        print_str("ticks=");
        print_uint64_dec(pit_ticks());
        print_char('\n');
    } else if (fn_streq(cmd, "time")) {
        console_print_time();
    } else if (fn_streq(cmd, "uptime")) {
        console_print_uptime();
    } else if (fn_streq(cmd, "about")) {
        console_print_about();
    } else if (fn_streq(cmd, "version")) {
        console_print_version();
    } else if (fn_streq(cmd, "panic_de")) {
        print_str("Triggering #DE...\n");
        console_trigger_panic_de();
    } else if (fn_streq(cmd, "panic_gp")) {
        print_str("Triggering #GP...\n");
        console_trigger_panic_gp();
    } else if (fn_streq(cmd, "panic_pf")) {
        print_str("Triggering #PF...\n");
        console_trigger_panic_pf();
    } else if (fn_streq(cmd, "echo")) {
        const char* rest = fn_rest_after_spaces(g_console_line, after_cmd_pos);
        print_str(rest);
        print_char('\n');
    } else if (fn_streq(cmd, "pwd")) {
        ramfs_cmd_pwd();
    } else if (fn_streq(cmd, "ls")) {
        ramfs_cmd_ls(has_arg1 ? arg1 : "");
    } else if (fn_streq(cmd, "tree")) {
        ramfs_cmd_tree(has_arg1 ? arg1 : "");
    } else if (fn_streq(cmd, "cd")) {
        ramfs_cmd_cd(has_arg1 ? arg1 : "");
    } else if (fn_streq(cmd, "mkdir")) {
        ramfs_cmd_mkdir(has_arg1 ? arg1 : "");
    } else if (fn_streq(cmd, "touch")) {
        ramfs_cmd_touch(has_arg1 ? arg1 : "");
    } else if (fn_streq(cmd, "mv")) {
        ramfs_cmd_mv(has_arg1 ? arg1 : "", has_arg2 ? arg2 : "");
    } else {
        print_str("Unknown command: ");
        print_str(g_console_line);
        print_char('\n');
    }

    console_reset_line();
    console_print_prompt();
    console_cursor_refresh_after_output();
}

void console_handle_char(char ch) {
    console_cursor_hide();
    print_set_color(PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
    if (ch == '\n') {
        console_submit_line();
        return;
    }
    if (g_console_line_len >= (CONSOLE_LINE_MAX - 1)) {
        console_cursor_refresh_after_output();
        return;
    }
    g_console_line[g_console_line_len++] = ch;
    g_console_line[g_console_line_len] = '\0';
    print_char(ch);
    g_console_history_nav = 0xFF;
    console_cursor_refresh_after_output();
}

void console_handle_backspace() {
    console_cursor_hide();
    if (g_console_line_len == 0) {
        console_cursor_refresh_after_output();
        return;
    }
    g_console_line_len--;
    g_console_line[g_console_line_len] = '\0';
    print_set_color(PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
    print_backspace();
    g_console_history_nav = 0xFF;
    console_cursor_refresh_after_output();
}

void console_history_prev() {
    if (g_console_history_count == 0) {
        return;
    }
    if (g_console_history_nav == 0xFF) {
        fn_strcopy(g_console_history_scratch, g_console_line, CONSOLE_LINE_MAX);
        g_console_history_nav = 0;
    } else if (g_console_history_nav + 1 < g_console_history_count) {
        g_console_history_nav++;
    } else {
        return;
    }
    uint8_t slot = console_history_slot_from_nav(g_console_history_nav);
    console_replace_current_line(g_console_history[slot]);
}

void console_history_next() {
    if (g_console_history_nav == 0xFF) {
        return;
    }
    if (g_console_history_nav == 0) {
        g_console_history_nav = 0xFF;
        console_replace_current_line(g_console_history_scratch);
        return;
    }
    g_console_history_nav--;
    uint8_t slot = console_history_slot_from_nav(g_console_history_nav);
    console_replace_current_line(g_console_history[slot]);
}

void console_clear_screen() {
    console_cursor_hide();
    print_clear();
    console_render_status_line();
    console_reset_line();
    console_print_prompt();
    console_cursor_refresh_after_output();
}

void console_render_status_line() {
    uint64_t ticks = pit_ticks();
    uint64_t pit_seconds = ticks / CONSOLE_PIT_HZ;
    uint8_t rtc_s = rtc_seconds();

    if (ticks == g_last_status_ticks && rtc_s == g_last_status_rtc_second) {
        return;
    }
    g_last_status_ticks = ticks;
    g_last_status_rtc_second = rtc_s;

    uint8_t rtc_h = rtc_hours();
    uint8_t rtc_m = rtc_minutes();
    char line[STATUS_BUF_MAX];
    size_t len = 0;
    line[0] = '\0';

    buf_append_str(line, &len, " STATUS | PIT ");
    buf_append_u64_dec(line, &len, pit_seconds);
    buf_append_str(line, &len, "s | RTC ");
    buf_append_char(line, &len, (char) ('0' + ((rtc_h / 10) % 10)));
    buf_append_char(line, &len, (char) ('0' + (rtc_h % 10)));
    buf_append_char(line, &len, ':');
    buf_append_char(line, &len, (char) ('0' + ((rtc_m / 10) % 10)));
    buf_append_char(line, &len, (char) ('0' + (rtc_m % 10)));
    buf_append_char(line, &len, ':');
    buf_append_char(line, &len, (char) ('0' + ((rtc_s / 10) % 10)));
    buf_append_char(line, &len, (char) ('0' + (rtc_s % 10)));

    print_clear_row_at(STATUS_ROW, PRINT_COLOR_BLACK, PRINT_COLOR_LIGHT_GRAY);
    print_write_str_at(STATUS_ROW, 0, line, PRINT_COLOR_BLACK, PRINT_COLOR_LIGHT_GRAY);
}

void console_tick() {
    uint64_t ticks = pit_ticks();
    console_render_status_line();
    if ((ticks - g_last_cursor_blink_tick) < CURSOR_BLINK_TICKS) {
        return;
    }
    g_last_cursor_blink_tick = ticks;
    if (g_cursor_visible) {
        console_cursor_hide();
    } else {
        console_cursor_show();
    }
}

void console_init() {
    ramfs_init();
    console_reset_line();
    g_console_history_nav = 0xFF;
    print_set_color(PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
    print_str("Console ready. Type 'help'.");
    console_print_prompt();
    console_cursor_refresh_after_output();
}
