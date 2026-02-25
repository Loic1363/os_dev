#include "bool.h"
#include "console.h"
#include "print.h"
#include "x86_64/pit.h"
#include "x86_64/rtc.h"

#define CONSOLE_LINE_MAX 64
#define CONSOLE_HISTORY_MAX 8
#define STATUS_ROW 0
#define STATUS_BUF_MAX 80
#define CONSOLE_PIT_HZ 100
#define CURSOR_BLINK_TICKS 25

#define FS_MAX_NODES 128
#define FS_NAME_MAX 16
#define CMD_TOKEN_MAX 32
#define PATH_BUF_MAX 96

enum {
    FS_NODE_UNUSED = 0,
    FS_NODE_DIR = 1,
    FS_NODE_FILE = 2,
};

struct FsNode {
    uint8_t used;
    uint8_t type;
    int16_t parent;
    int16_t first_child;
    int16_t next_sibling;
    char name[FS_NAME_MAX];
};

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

static struct FsNode g_fs_nodes[FS_MAX_NODES];
static int16_t g_fs_root = -1;
static int16_t g_fs_cwd = -1;

static bool streq(const char* a, const char* b) {
    for (size_t i = 0; ; i++) {
        if (a[i] != b[i]) {
            return false;
        }
        if (a[i] == '\0') {
            return true;
        }
    }
}

static bool strstarts(const char* str, const char* prefix) {
    for (size_t i = 0; ; i++) {
        if (prefix[i] == '\0') {
            return true;
        }
        if (str[i] != prefix[i]) {
            return false;
        }
    }
}

static size_t str_len(const char* str) {
    size_t n = 0;
    while (str[n] != '\0') {
        n++;
    }
    return n;
}

static void strcopy(char* dst, const char* src, size_t max_len) {
    size_t i = 0;
    for (; i + 1 < max_len && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
}

static bool name_eq(const char* a, const char* b) {
    return streq(a, b);
}

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

static void skip_spaces(const char* s, size_t* pos) {
    while (s[*pos] == ' ') {
        (*pos)++;
    }
}

static bool next_token(const char* s, size_t* pos, char* out, size_t out_max) {
    skip_spaces(s, pos);
    if (s[*pos] == '\0') {
        out[0] = '\0';
        return false;
    }

    size_t i = 0;
    while (s[*pos] != '\0' && s[*pos] != ' ') {
        if (i + 1 < out_max) {
            out[i++] = s[*pos];
        }
        (*pos)++;
    }
    out[i] = '\0';
    return true;
}

static const char* rest_after_spaces(const char* s, size_t pos) {
    while (s[pos] == ' ') {
        pos++;
    }
    return s + pos;
}

static void console_history_store_current() {
    if (g_console_line_len == 0) {
        return;
    }

    strcopy(g_console_history[g_console_history_next_slot], g_console_line, CONSOLE_LINE_MAX);
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

static void fs_zero_all() {
    for (size_t i = 0; i < FS_MAX_NODES; i++) {
        g_fs_nodes[i].used = 0;
        g_fs_nodes[i].type = FS_NODE_UNUSED;
        g_fs_nodes[i].parent = -1;
        g_fs_nodes[i].first_child = -1;
        g_fs_nodes[i].next_sibling = -1;
        g_fs_nodes[i].name[0] = '\0';
    }
}

static int16_t fs_alloc_node(uint8_t type, const char* name, int16_t parent) {
    for (int16_t i = 0; i < FS_MAX_NODES; i++) {
        if (!g_fs_nodes[i].used) {
            g_fs_nodes[i].used = 1;
            g_fs_nodes[i].type = type;
            g_fs_nodes[i].parent = parent;
            g_fs_nodes[i].first_child = -1;
            g_fs_nodes[i].next_sibling = -1;
            strcopy(g_fs_nodes[i].name, name, FS_NAME_MAX);
            return i;
        }
    }
    return -1;
}

static int16_t fs_find_child(int16_t parent, const char* name) {
    if (parent < 0) {
        return -1;
    }

    int16_t child = g_fs_nodes[parent].first_child;
    while (child >= 0) {
        if (name_eq(g_fs_nodes[child].name, name)) {
            return child;
        }
        child = g_fs_nodes[child].next_sibling;
    }
    return -1;
}

static bool fs_attach_child(int16_t parent, int16_t child) {
    if (parent < 0 || child < 0) {
        return false;
    }
    if (g_fs_nodes[parent].type != FS_NODE_DIR) {
        return false;
    }

    g_fs_nodes[child].parent = parent;
    g_fs_nodes[child].next_sibling = -1;

    if (g_fs_nodes[parent].first_child < 0) {
        g_fs_nodes[parent].first_child = child;
        return true;
    }

    int16_t it = g_fs_nodes[parent].first_child;
    while (g_fs_nodes[it].next_sibling >= 0) {
        it = g_fs_nodes[it].next_sibling;
    }
    g_fs_nodes[it].next_sibling = child;
    return true;
}

static bool fs_detach_child(int16_t child) {
    if (child < 0) {
        return false;
    }

    int16_t parent = g_fs_nodes[child].parent;
    if (parent < 0) {
        return false;
    }

    int16_t prev = -1;
    int16_t it = g_fs_nodes[parent].first_child;
    while (it >= 0) {
        if (it == child) {
            if (prev < 0) {
                g_fs_nodes[parent].first_child = g_fs_nodes[it].next_sibling;
            } else {
                g_fs_nodes[prev].next_sibling = g_fs_nodes[it].next_sibling;
            }
            g_fs_nodes[child].next_sibling = -1;
            return true;
        }
        prev = it;
        it = g_fs_nodes[it].next_sibling;
    }
    return false;
}

static bool fs_name_valid(const char* name) {
    if (name[0] == '\0') {
        return false;
    }

    for (size_t i = 0; name[i] != '\0'; i++) {
        if (name[i] == '/') {
            return false;
        }
    }

    if (streq(name, ".") || streq(name, "..")) {
        return false;
    }

    return true;
}

static bool fs_next_path_component(const char* path, size_t* pos, char* out, size_t out_max) {
    while (path[*pos] == '/') {
        (*pos)++;
    }

    if (path[*pos] == '\0') {
        out[0] = '\0';
        return false;
    }

    size_t i = 0;
    while (path[*pos] != '\0' && path[*pos] != '/') {
        if (i + 1 < out_max) {
            out[i++] = path[*pos];
        }
        (*pos)++;
    }
    out[i] = '\0';
    return true;
}

static bool fs_resolve_path_from(int16_t start, const char* path, int16_t* out_node) {
    int16_t current = (path[0] == '/') ? g_fs_root : start;
    size_t pos = 0;
    char component[FS_NAME_MAX];

    if (current < 0) {
        return false;
    }

    while (fs_next_path_component(path, &pos, component, sizeof(component))) {
        if (streq(component, ".")) {
            continue;
        }

        if (streq(component, "..")) {
            if (g_fs_nodes[current].parent >= 0) {
                current = g_fs_nodes[current].parent;
            }
            continue;
        }

        int16_t child = fs_find_child(current, component);
        if (child < 0) {
            return false;
        }
        current = child;
    }

    *out_node = current;
    return true;
}

static bool fs_resolve_path(const char* path, int16_t* out_node) {
    return fs_resolve_path_from(g_fs_cwd, path, out_node);
}

static bool fs_resolve_parent_for_create(const char* path, int16_t* out_parent, char* out_name) {
    size_t len = str_len(path);
    if (len == 0) {
        return false;
    }

    while (len > 0 && path[len - 1] == '/') {
        len--;
    }
    if (len == 0) {
        return false;
    }

    size_t split = len;
    while (split > 0 && path[split - 1] != '/') {
        split--;
    }

    size_t name_len = len - split;
    if (name_len == 0 || name_len >= FS_NAME_MAX) {
        return false;
    }

    for (size_t i = 0; i < name_len; i++) {
        out_name[i] = path[split + i];
    }
    out_name[name_len] = '\0';

    if (!fs_name_valid(out_name)) {
        return false;
    }

    if (split == 0) {
        *out_parent = (path[0] == '/') ? g_fs_root : g_fs_cwd;
        return true;
    }

    char parent_path[PATH_BUF_MAX];
    if (split >= PATH_BUF_MAX) {
        return false;
    }

    for (size_t i = 0; i < split; i++) {
        parent_path[i] = path[i];
    }
    parent_path[split] = '\0';

    return fs_resolve_path(parent_path, out_parent);
}

static int16_t fs_create_child(int16_t parent, const char* name, uint8_t type) {
    if (parent < 0 || g_fs_nodes[parent].type != FS_NODE_DIR) {
        return -1;
    }
    if (!fs_name_valid(name)) {
        return -1;
    }
    if (fs_find_child(parent, name) >= 0) {
        return -1;
    }

    int16_t node = fs_alloc_node(type, name, parent);
    if (node < 0) {
        return -1;
    }
    if (!fs_attach_child(parent, node)) {
        g_fs_nodes[node].used = 0;
        return -1;
    }
    return node;
}

static bool fs_is_ancestor(int16_t ancestor, int16_t node) {
    int16_t it = node;
    while (it >= 0) {
        if (it == ancestor) {
            return true;
        }
        it = g_fs_nodes[it].parent;
    }
    return false;
}

static bool fs_build_path(int16_t node, char* out, size_t out_max) {
    if (out_max == 0 || node < 0) {
        return false;
    }

    if (node == g_fs_root) {
        if (out_max < 2) {
            return false;
        }
        out[0] = '/';
        out[1] = '\0';
        return true;
    }

    int16_t stack[FS_MAX_NODES];
    size_t depth = 0;
    int16_t it = node;
    while (it >= 0 && it != g_fs_root && depth < FS_MAX_NODES) {
        stack[depth++] = it;
        it = g_fs_nodes[it].parent;
    }

    size_t pos = 0;
    out[pos++] = '/';
    for (size_t i = 0; i < depth; i++) {
        int16_t n = stack[depth - 1 - i];
        const char* name = g_fs_nodes[n].name;
        for (size_t j = 0; name[j] != '\0'; j++) {
            if (pos + 1 >= out_max) {
                out[out_max - 1] = '\0';
                return false;
            }
            out[pos++] = name[j];
        }
        if (i + 1 < depth) {
            if (pos + 1 >= out_max) {
                out[out_max - 1] = '\0';
                return false;
            }
            out[pos++] = '/';
        }
    }
    if (pos >= out_max) {
        out[out_max - 1] = '\0';
        return false;
    }
    out[pos] = '\0';
    return true;
}

static void fs_print_pwd() {
    char path[PATH_BUF_MAX];
    if (fs_build_path(g_fs_cwd, path, sizeof(path))) {
        print_str(path);
        print_char('\n');
    } else {
        print_str("/\n");
    }
}

static void fs_print_ls_dir(int16_t dir) {
    int16_t child = g_fs_nodes[dir].first_child;
    if (child < 0) {
        print_str("(empty)\n");
        return;
    }

    while (child >= 0) {
        print_str(g_fs_nodes[child].name);
        if (g_fs_nodes[child].type == FS_NODE_DIR) {
            print_char('/');
        }
        if (g_fs_nodes[child].next_sibling >= 0) {
            print_char(' ');
        }
        child = g_fs_nodes[child].next_sibling;
    }
    print_char('\n');
}

static void fs_print_tree_rec(int16_t dir, uint8_t depth) {
    int16_t child = g_fs_nodes[dir].first_child;
    while (child >= 0) {
        for (uint8_t i = 0; i < depth; i++) {
            print_str("  ");
        }
        print_str("- ");
        print_str(g_fs_nodes[child].name);
        if (g_fs_nodes[child].type == FS_NODE_DIR) {
            print_char('/');
        }
        print_char('\n');

        if (g_fs_nodes[child].type == FS_NODE_DIR) {
            fs_print_tree_rec(child, (uint8_t) (depth + 1));
        }
        child = g_fs_nodes[child].next_sibling;
    }
}

static void fs_cmd_pwd() {
    fs_print_pwd();
}

static void fs_cmd_ls(const char* path_opt) {
    int16_t node;
    if (path_opt == NULL || path_opt[0] == '\0') {
        node = g_fs_cwd;
    } else if (!fs_resolve_path(path_opt, &node)) {
        print_str("ls: path not found\n");
        return;
    }

    if (g_fs_nodes[node].type == FS_NODE_FILE) {
        print_str(g_fs_nodes[node].name);
        print_char('\n');
        return;
    }

    fs_print_ls_dir(node);
}

static void fs_cmd_tree(const char* path_opt) {
    int16_t node;
    if (path_opt == NULL || path_opt[0] == '\0') {
        node = g_fs_cwd;
    } else if (!fs_resolve_path(path_opt, &node)) {
        print_str("tree: path not found\n");
        return;
    }

    if (g_fs_nodes[node].type == FS_NODE_FILE) {
        print_str("tree: not a directory\n");
        return;
    }

    char path[PATH_BUF_MAX];
    if (fs_build_path(node, path, sizeof(path))) {
        print_str(path);
        print_char('\n');
    }
    fs_print_tree_rec(node, 0);
}

static void fs_cmd_cd(const char* path_opt) {
    int16_t node;
    const char* path = (path_opt == NULL || path_opt[0] == '\0') ? "/" : path_opt;

    if (!fs_resolve_path(path, &node)) {
        print_str("cd: path not found\n");
        return;
    }

    if (g_fs_nodes[node].type != FS_NODE_DIR) {
        print_str("cd: not a directory\n");
        return;
    }

    g_fs_cwd = node;
}

static void fs_cmd_create(const char* path, uint8_t type, const char* cmd_name) {
    int16_t parent;
    char name[FS_NAME_MAX];

    if (path == NULL || path[0] == '\0') {
        print_str(cmd_name);
        print_str(": missing operand\n");
        return;
    }

    if (!fs_resolve_parent_for_create(path, &parent, name)) {
        print_str(cmd_name);
        print_str(": invalid path\n");
        return;
    }

    if (parent < 0 || g_fs_nodes[parent].type != FS_NODE_DIR) {
        print_str(cmd_name);
        print_str(": parent is not a directory\n");
        return;
    }

    if (fs_create_child(parent, name, type) < 0) {
        print_str(cmd_name);
        print_str(": create failed\n");
    }
}

static void fs_cmd_mkdir(const char* path) {
    fs_cmd_create(path, FS_NODE_DIR, "mkdir");
}

static void fs_cmd_touch(const char* path) {
    fs_cmd_create(path, FS_NODE_FILE, "touch");
}

static void fs_cmd_mv(const char* src_path, const char* dst_path) {
    int16_t src;
    if (src_path == NULL || dst_path == NULL || src_path[0] == '\0' || dst_path[0] == '\0') {
        print_str("mv: missing operand\n");
        return;
    }

    if (!fs_resolve_path(src_path, &src)) {
        print_str("mv: source not found\n");
        return;
    }

    if (src == g_fs_root) {
        print_str("mv: cannot move root\n");
        return;
    }

    int16_t dst_existing;
    int16_t new_parent = -1;
    char new_name[FS_NAME_MAX];

    if (fs_resolve_path(dst_path, &dst_existing)) {
        if (g_fs_nodes[dst_existing].type != FS_NODE_DIR) {
            print_str("mv: destination exists and is not a directory\n");
            return;
        }
        new_parent = dst_existing;
        strcopy(new_name, g_fs_nodes[src].name, sizeof(new_name));
    } else {
        if (!fs_resolve_parent_for_create(dst_path, &new_parent, new_name)) {
            print_str("mv: invalid destination path\n");
            return;
        }
    }

    if (new_parent < 0 || g_fs_nodes[new_parent].type != FS_NODE_DIR) {
        print_str("mv: destination parent is not a directory\n");
        return;
    }

    if (g_fs_nodes[src].type == FS_NODE_DIR && fs_is_ancestor(src, new_parent)) {
        print_str("mv: cannot move a directory into itself\n");
        return;
    }

    int16_t collision = fs_find_child(new_parent, new_name);
    if (collision >= 0 && collision != src) {
        print_str("mv: destination already exists\n");
        return;
    }

    int16_t old_parent = g_fs_nodes[src].parent;
    if (old_parent != new_parent) {
        if (!fs_detach_child(src)) {
            print_str("mv: detach failed\n");
            return;
        }
        if (!fs_attach_child(new_parent, src)) {
            print_str("mv: attach failed\n");
            return;
        }
    }

    strcopy(g_fs_nodes[src].name, new_name, FS_NAME_MAX);
}

static void fs_seed_demo_tree() {
    int16_t lib = fs_create_child(g_fs_root, "lib", FS_NODE_DIR);
    int16_t x86 = fs_create_child(lib, "x86_64", FS_NODE_DIR);
    int16_t funcs = fs_create_child(x86, "functions", FS_NODE_DIR);
    fs_create_child(funcs, "print", FS_NODE_FILE);
    fs_create_child(funcs, "port", FS_NODE_FILE);
    fs_create_child(funcs, "idt", FS_NODE_FILE);

    fs_create_child(g_fs_root, "bin", FS_NODE_DIR);
    fs_create_child(g_fs_root, "tmp", FS_NODE_DIR);
    int16_t home = fs_create_child(g_fs_root, "home", FS_NODE_DIR);
    fs_create_child(home, "user", FS_NODE_DIR);
}

static void fs_init() {
    fs_zero_all();
    g_fs_root = fs_alloc_node(FS_NODE_DIR, "", -1);
    g_fs_cwd = g_fs_root;
    fs_seed_demo_tree();
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
    next_token(g_console_line, &pos, cmd, sizeof(cmd));

    size_t after_cmd_pos = pos;
    bool has_arg1 = next_token(g_console_line, &pos, arg1, sizeof(arg1));
    bool has_arg2 = next_token(g_console_line, &pos, arg2, sizeof(arg2));

    if (streq(cmd, "help")) {
        print_str("Commands: help clear cls ticks time uptime echo about version panic_de panic_gp panic_pf pwd ls tree cd mkdir touch mv\n");
    } else if (streq(cmd, "clear") || streq(cmd, "cls")) {
        print_clear();
        console_render_status_line();
    } else if (streq(cmd, "ticks")) {
        print_str("ticks=");
        print_uint64_dec(pit_ticks());
        print_char('\n');
    } else if (streq(cmd, "time")) {
        console_print_time();
    } else if (streq(cmd, "uptime")) {
        console_print_uptime();
    } else if (streq(cmd, "about")) {
        console_print_about();
    } else if (streq(cmd, "version")) {
        console_print_version();
    } else if (streq(cmd, "panic_de")) {
        print_str("Triggering #DE...\n");
        console_trigger_panic_de();
    } else if (streq(cmd, "panic_gp")) {
        print_str("Triggering #GP...\n");
        console_trigger_panic_gp();
    } else if (streq(cmd, "panic_pf")) {
        print_str("Triggering #PF...\n");
        console_trigger_panic_pf();
    } else if (streq(cmd, "echo")) {
        const char* rest = rest_after_spaces(g_console_line, after_cmd_pos);
        print_str((char*) rest);
        print_char('\n');
    } else if (streq(cmd, "pwd")) {
        fs_cmd_pwd();
    } else if (streq(cmd, "ls")) {
        fs_cmd_ls(has_arg1 ? arg1 : "");
    } else if (streq(cmd, "tree")) {
        fs_cmd_tree(has_arg1 ? arg1 : "");
    } else if (streq(cmd, "cd")) {
        fs_cmd_cd(has_arg1 ? arg1 : "");
    } else if (streq(cmd, "mkdir")) {
        fs_cmd_mkdir(has_arg1 ? arg1 : "");
    } else if (streq(cmd, "touch")) {
        fs_cmd_touch(has_arg1 ? arg1 : "");
    } else if (streq(cmd, "mv")) {
        fs_cmd_mv(has_arg1 ? arg1 : "", has_arg2 ? arg2 : "");
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
        strcopy(g_console_history_scratch, g_console_line, CONSOLE_LINE_MAX);
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
    fs_init();
    console_reset_line();
    g_console_history_nav = 0xFF;
    print_set_color(PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
    print_str("Console ready. Type 'help'.");
    console_print_prompt();
    console_cursor_refresh_after_output();
}
