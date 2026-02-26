#include "print.h"
#include "lib/x86_64/functions/ramfs.h"
#include "lib/x86_64/functions/string.h"

#define FS_MAX_NODES 128
#define FS_NAME_MAX 16
#define PATH_BUF_MAX 96
#define FS_FILE_CONTENT_MAX 2048

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
    uint16_t content_len;
    char content[FS_FILE_CONTENT_MAX];
};

static struct FsNode g_fs_nodes[FS_MAX_NODES];
static int16_t g_fs_root = -1;
static int16_t g_fs_cwd = -1;
static int16_t g_fs_home = -1;

static void fs_zero_all() {
    for (size_t i = 0; i < FS_MAX_NODES; i++) {
        g_fs_nodes[i].used = 0;
        g_fs_nodes[i].type = FS_NODE_UNUSED;
        g_fs_nodes[i].parent = -1;
        g_fs_nodes[i].first_child = -1;
        g_fs_nodes[i].next_sibling = -1;
        g_fs_nodes[i].name[0] = '\0';
        g_fs_nodes[i].content_len = 0;
        g_fs_nodes[i].content[0] = '\0';
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
            fn_strcopy(g_fs_nodes[i].name, name, FS_NAME_MAX);
            g_fs_nodes[i].content_len = 0;
            g_fs_nodes[i].content[0] = '\0';
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
        if (fn_streq(g_fs_nodes[child].name, name)) {
            return child;
        }
        child = g_fs_nodes[child].next_sibling;
    }
    return -1;
}

static uint8_t fs_attach_child(int16_t parent, int16_t child) {
    if (parent < 0 || child < 0 || g_fs_nodes[parent].type != FS_NODE_DIR) {
        return 0;
    }

    g_fs_nodes[child].parent = parent;
    g_fs_nodes[child].next_sibling = -1;
    if (g_fs_nodes[parent].first_child < 0) {
        g_fs_nodes[parent].first_child = child;
        return 1;
    }

    int16_t it = g_fs_nodes[parent].first_child;
    while (g_fs_nodes[it].next_sibling >= 0) {
        it = g_fs_nodes[it].next_sibling;
    }
    g_fs_nodes[it].next_sibling = child;
    return 1;
}

static uint8_t fs_detach_child(int16_t child) {
    int16_t parent = (child >= 0) ? g_fs_nodes[child].parent : -1;
    if (parent < 0) {
        return 0;
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
            return 1;
        }
        prev = it;
        it = g_fs_nodes[it].next_sibling;
    }
    return 0;
}

static uint8_t fs_name_valid(const char* name) {
    if (name[0] == '\0' || fn_streq(name, ".") || fn_streq(name, "..")) {
        return 0;
    }
    for (size_t i = 0; name[i] != '\0'; i++) {
        if (name[i] == '/') {
            return 0;
        }
    }
    return 1;
}

static uint8_t fs_next_path_component(const char* path, size_t* pos, char* out, size_t out_max) {
    while (path[*pos] == '/') {
        (*pos)++;
    }
    if (path[*pos] == '\0') {
        out[0] = '\0';
        return 0;
    }

    size_t i = 0;
    while (path[*pos] != '\0' && path[*pos] != '/') {
        if (i + 1 < out_max) {
            out[i++] = path[*pos];
        }
        (*pos)++;
    }
    out[i] = '\0';
    return 1;
}

static uint8_t fs_resolve_path_from(int16_t start, const char* path, int16_t* out_node) {
    int16_t current = (path[0] == '/') ? g_fs_root : start;
    size_t pos = 0;
    char component[FS_NAME_MAX];

    if (current < 0) {
        return 0;
    }

    while (fs_next_path_component(path, &pos, component, sizeof(component))) {
        if (fn_streq(component, ".")) {
            continue;
        }
        if (fn_streq(component, "..")) {
            if (g_fs_nodes[current].parent >= 0) {
                current = g_fs_nodes[current].parent;
            }
            continue;
        }
        int16_t child = fs_find_child(current, component);
        if (child < 0) {
            return 0;
        }
        current = child;
    }

    *out_node = current;
    return 1;
}

static uint8_t fs_resolve_path(const char* path, int16_t* out_node) {
    return fs_resolve_path_from(g_fs_cwd, path, out_node);
}

static uint8_t fs_resolve_parent_for_create(const char* path, int16_t* out_parent, char* out_name) {
    size_t len = fn_strlen(path);
    if (len == 0) {
        return 0;
    }
    while (len > 0 && path[len - 1] == '/') {
        len--;
    }
    if (len == 0) {
        return 0;
    }

    size_t split = len;
    while (split > 0 && path[split - 1] != '/') {
        split--;
    }

    size_t name_len = len - split;
    if (name_len == 0 || name_len >= FS_NAME_MAX) {
        return 0;
    }

    for (size_t i = 0; i < name_len; i++) {
        out_name[i] = path[split + i];
    }
    out_name[name_len] = '\0';

    if (!fs_name_valid(out_name)) {
        return 0;
    }

    if (split == 0) {
        *out_parent = (path[0] == '/') ? g_fs_root : g_fs_cwd;
        return 1;
    }

    char parent_path[PATH_BUF_MAX];
    if (split >= PATH_BUF_MAX) {
        return 0;
    }
    for (size_t i = 0; i < split; i++) {
        parent_path[i] = path[i];
    }
    parent_path[split] = '\0';
    return fs_resolve_path(parent_path, out_parent);
}

static int16_t fs_create_child(int16_t parent, const char* name, uint8_t type) {
    if (parent < 0 || g_fs_nodes[parent].type != FS_NODE_DIR || !fs_name_valid(name)) {
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

static uint8_t fs_is_ancestor(int16_t ancestor, int16_t node) {
    int16_t it = node;
    while (it >= 0) {
        if (it == ancestor) {
            return 1;
        }
        it = g_fs_nodes[it].parent;
    }
    return 0;
}

static void fs_free_node_shallow(int16_t node) {
    if (node < 0) {
        return;
    }
    g_fs_nodes[node].used = 0;
    g_fs_nodes[node].type = FS_NODE_UNUSED;
    g_fs_nodes[node].parent = -1;
    g_fs_nodes[node].first_child = -1;
    g_fs_nodes[node].next_sibling = -1;
    g_fs_nodes[node].name[0] = '\0';
    g_fs_nodes[node].content_len = 0;
    g_fs_nodes[node].content[0] = '\0';
}

static uint16_t fs_dir_child_count(int16_t dir) {
    uint16_t count = 0;
    int16_t child = (dir >= 0) ? g_fs_nodes[dir].first_child : -1;
    while (child >= 0) {
        count++;
        child = g_fs_nodes[child].next_sibling;
    }
    return count;
}

static void fs_copy_file_content(int16_t dst, int16_t src) {
    if (dst < 0 || src < 0) {
        return;
    }
    g_fs_nodes[dst].content_len = g_fs_nodes[src].content_len;
    for (size_t i = 0; i < g_fs_nodes[src].content_len; i++) {
        g_fs_nodes[dst].content[i] = g_fs_nodes[src].content[i];
    }
    g_fs_nodes[dst].content[g_fs_nodes[src].content_len] = '\0';
}

static uint8_t fs_remove_node_recursive_internal(int16_t node) {
    if (node < 0 || node == g_fs_root || node == g_fs_home) {
        return 0;
    }
    if (fs_is_ancestor(node, g_fs_cwd)) {
        return 0;
    }

    while (g_fs_nodes[node].first_child >= 0) {
        int16_t child = g_fs_nodes[node].first_child;
        if (!fs_remove_node_recursive_internal(child)) {
            return 0;
        }
    }

    if (!fs_detach_child(node)) {
        return 0;
    }
    fs_free_node_shallow(node);
    return 1;
}

static int16_t fs_clone_subtree_into(int16_t src, int16_t dst_parent, const char* dst_name) {
    if (src < 0 || dst_parent < 0) {
        return -1;
    }

    int16_t created = fs_create_child(dst_parent, dst_name, g_fs_nodes[src].type);
    if (created < 0) {
        return -1;
    }

    if (g_fs_nodes[src].type == FS_NODE_FILE) {
        fs_copy_file_content(created, src);
        return created;
    }

    int16_t child = g_fs_nodes[src].first_child;
    while (child >= 0) {
        int16_t next = g_fs_nodes[child].next_sibling;
        if (fs_clone_subtree_into(child, created, g_fs_nodes[child].name) < 0) {
            fs_remove_node_recursive_internal(created);
            return -1;
        }
        child = next;
    }
    return created;
}

static void fs_collect_stats(struct RamfsStats* out_stats) {
    out_stats->total_nodes = FS_MAX_NODES;
    out_stats->used_nodes = 0;
    out_stats->dir_nodes = 0;
    out_stats->file_nodes = 0;
    out_stats->file_bytes = 0;

    for (size_t i = 0; i < FS_MAX_NODES; i++) {
        if (!g_fs_nodes[i].used) {
            continue;
        }
        out_stats->used_nodes++;
        if (g_fs_nodes[i].type == FS_NODE_DIR) {
            out_stats->dir_nodes++;
        } else if (g_fs_nodes[i].type == FS_NODE_FILE) {
            out_stats->file_nodes++;
            out_stats->file_bytes += g_fs_nodes[i].content_len;
        }
    }
}

static uint8_t fs_build_path(int16_t node, char* out, size_t out_max) {
    if (out_max < 2 || node < 0) {
        return 0;
    }
    if (node == g_fs_root) {
        out[0] = '/';
        out[1] = '\0';
        return 1;
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
                return 0;
            }
            out[pos++] = name[j];
        }
        if (i + 1 < depth) {
            if (pos + 1 >= out_max) {
                out[out_max - 1] = '\0';
                return 0;
            }
            out[pos++] = '/';
        }
    }
    out[pos] = '\0';
    return 1;
}

static uint8_t fs_build_prompt_path(char* out, size_t out_max) {
    char full[PATH_BUF_MAX];
    char home[PATH_BUF_MAX];

    if (!fs_build_path(g_fs_cwd, full, sizeof(full))) {
        return 0;
    }
    if (g_fs_home >= 0 && fs_build_path(g_fs_home, home, sizeof(home))) {
        if (fn_streq(full, home)) {
            if (out_max < 2) {
                return 0;
            }
            out[0] = '~';
            out[1] = '\0';
            return 1;
        }

        if (fn_strstarts(full, home) && full[fn_strlen(home)] == '/') {
            size_t pos = 0;
            if (out_max < 2) {
                return 0;
            }
            out[pos++] = '~';
            for (size_t i = fn_strlen(home); full[i] != '\0'; i++) {
                if (pos + 1 >= out_max) {
                    out[out_max - 1] = '\0';
                    return 0;
                }
                out[pos++] = full[i];
            }
            out[pos] = '\0';
            return 1;
        }
    }

    fn_strcopy(out, full, out_max);
    return 1;
}

static uint8_t fs_starts_with(const char* s, const char* prefix) {
    return fn_strstarts(s, prefix);
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

static void fs_seed_demo_tree() {
    int16_t lib = fs_create_child(g_fs_root, "lib", FS_NODE_DIR);
    int16_t x86 = fs_create_child(lib, "x86_64", FS_NODE_DIR);
    int16_t funcs = fs_create_child(x86, "functions", FS_NODE_DIR);
    fs_create_child(funcs, "print.c", FS_NODE_FILE);
    fs_create_child(funcs, "port.c", FS_NODE_FILE);
    fs_create_child(funcs, "idt.c", FS_NODE_FILE);
    fs_create_child(g_fs_root, "bin", FS_NODE_DIR);
    fs_create_child(g_fs_root, "tmp", FS_NODE_DIR);
    int16_t home = fs_create_child(g_fs_root, "home", FS_NODE_DIR);
    g_fs_home = fs_create_child(home, "admin", FS_NODE_DIR);
}

void ramfs_init() {
    fs_zero_all();
    g_fs_root = fs_alloc_node(FS_NODE_DIR, "", -1);
    g_fs_cwd = g_fs_root;
    g_fs_home = -1;
    fs_seed_demo_tree();
    if (g_fs_home >= 0) {
        g_fs_cwd = g_fs_home;
    }
}

void ramfs_get_prompt_cwd(char* out, size_t out_max) {
    if (out_max == 0) {
        return;
    }
    if (!fs_build_prompt_path(out, out_max)) {
        out[0] = '~';
        if (out_max > 1) {
            out[1] = '\0';
        }
    }
}

unsigned char ramfs_complete_path(const char* input_path, char* out_path, size_t out_max) {
    if (input_path == NULL || out_path == NULL || out_max == 0) {
        return 0;
    }

    size_t in_len = fn_strlen(input_path);
    if (in_len == 0) {
        return 0;
    }

    size_t split = in_len;
    while (split > 0 && input_path[split - 1] != '/') {
        split--;
    }

    char prefix[FS_NAME_MAX];
    size_t prefix_len = in_len - split;
    if (prefix_len >= FS_NAME_MAX) {
        return 0;
    }
    for (size_t i = 0; i < prefix_len; i++) {
        prefix[i] = input_path[split + i];
    }
    prefix[prefix_len] = '\0';

    int16_t parent = -1;
    if (split == 0) {
        parent = (input_path[0] == '/') ? g_fs_root : g_fs_cwd;
    } else {
        char parent_path[PATH_BUF_MAX];
        if (split >= sizeof(parent_path)) {
            return 0;
        }
        for (size_t i = 0; i < split; i++) {
            parent_path[i] = input_path[i];
        }
        parent_path[split] = '\0';
        if (!fs_resolve_path(parent_path, &parent)) {
            return 0;
        }
    }

    if (parent < 0 || g_fs_nodes[parent].type != FS_NODE_DIR) {
        return 0;
    }

    int16_t match = -1;
    uint8_t match_count = 0;
    int16_t child = g_fs_nodes[parent].first_child;
    while (child >= 0) {
        if (fs_starts_with(g_fs_nodes[child].name, prefix)) {
            match = child;
            match_count++;
            if (match_count > 1) {
                return 0;
            }
        }
        child = g_fs_nodes[child].next_sibling;
    }

    if (match_count != 1 || match < 0) {
        return 0;
    }

    size_t out_pos = 0;
    for (size_t i = 0; i < split; i++) {
        if (out_pos + 1 >= out_max) {
            out_path[out_max - 1] = '\0';
            return 0;
        }
        out_path[out_pos++] = input_path[i];
    }

    const char* name = g_fs_nodes[match].name;
    for (size_t i = 0; name[i] != '\0'; i++) {
        if (out_pos + 1 >= out_max) {
            out_path[out_max - 1] = '\0';
            return 0;
        }
        out_path[out_pos++] = name[i];
    }

    out_path[out_pos] = '\0';
    return 1;
}

unsigned char ramfs_open_or_create_file(const char* path) {
    int16_t node;
    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    if (fs_resolve_path(path, &node)) {
        return (g_fs_nodes[node].type == FS_NODE_FILE) ? 1 : 0;
    }

    int16_t parent;
    char name[FS_NAME_MAX];
    if (!fs_resolve_parent_for_create(path, &parent, name)) {
        return 0;
    }

    return fs_create_child(parent, name, FS_NODE_FILE) >= 0;
}

unsigned char ramfs_read_file(const char* path, char* out, size_t out_max, size_t* out_len) {
    int16_t node;
    if (path == NULL || out == NULL || out_max == 0) {
        return 0;
    }
    if (!fs_resolve_path(path, &node)) {
        return 0;
    }
    if (g_fs_nodes[node].type != FS_NODE_FILE) {
        return 0;
    }

    size_t n = g_fs_nodes[node].content_len;
    if (n >= out_max) {
        n = out_max - 1;
    }
    for (size_t i = 0; i < n; i++) {
        out[i] = g_fs_nodes[node].content[i];
    }
    out[n] = '\0';
    if (out_len != NULL) {
        *out_len = n;
    }
    return 1;
}

unsigned char ramfs_write_file(const char* path, const char* data, size_t len) {
    int16_t node;
    if (path == NULL || data == NULL) {
        return 0;
    }
    if (!fs_resolve_path(path, &node)) {
        return 0;
    }
    if (g_fs_nodes[node].type != FS_NODE_FILE) {
        return 0;
    }

    if (len >= FS_FILE_CONTENT_MAX) {
        len = FS_FILE_CONTENT_MAX - 1;
    }
    for (size_t i = 0; i < len; i++) {
        g_fs_nodes[node].content[i] = data[i];
    }
    g_fs_nodes[node].content[len] = '\0';
    g_fs_nodes[node].content_len = (uint16_t) len;
    return 1;
}

void ramfs_cmd_pwd() {
    char path[PATH_BUF_MAX];
    if (fs_build_path(g_fs_cwd, path, sizeof(path))) {
        print_str(path);
    } else {
        print_str("/");
    }
    print_char('\n');
}

void ramfs_cmd_ls(const char* path_opt) {
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

void ramfs_cmd_tree(const char* path_opt) {
    int16_t node;
    if (path_opt == NULL || path_opt[0] == '\0') {
        node = g_fs_cwd;
    } else if (!fs_resolve_path(path_opt, &node)) {
        print_str("tree: path not found\n");
        return;
    }

    if (g_fs_nodes[node].type != FS_NODE_DIR) {
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

void ramfs_cmd_cd(const char* path_opt) {
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

static void ramfs_cmd_create(const char* path, uint8_t type, const char* cmd_name) {
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

void ramfs_cmd_mkdir(const char* path) {
    ramfs_cmd_create(path, FS_NODE_DIR, "mkdir");
}

void ramfs_cmd_touch(const char* path) {
    ramfs_cmd_create(path, FS_NODE_FILE, "touch");
}

void ramfs_cmd_mv(const char* src_path, const char* dst_path) {
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
        fn_strcopy(new_name, g_fs_nodes[src].name, sizeof(new_name));
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

    if (g_fs_nodes[src].parent != new_parent) {
        if (!fs_detach_child(src)) {
            print_str("mv: detach failed\n");
            return;
        }
        if (!fs_attach_child(new_parent, src)) {
            print_str("mv: attach failed\n");
            return;
        }
    }

    fn_strcopy(g_fs_nodes[src].name, new_name, FS_NAME_MAX);
}

void ramfs_cmd_cp(const char* src_path, const char* dst_path) {
    int16_t src;
    if (src_path == NULL || dst_path == NULL || src_path[0] == '\0' || dst_path[0] == '\0') {
        print_str("cp: missing operand\n");
        return;
    }

    if (!fs_resolve_path(src_path, &src)) {
        print_str("cp: source not found\n");
        return;
    }
    if (g_fs_nodes[src].type != FS_NODE_FILE) {
        print_str("cp: only file copy is supported\n");
        return;
    }

    int16_t dst_node = -1;
    if (fs_resolve_path(dst_path, &dst_node)) {
        if (g_fs_nodes[dst_node].type == FS_NODE_DIR) {
            int16_t existing = fs_find_child(dst_node, g_fs_nodes[src].name);
            if (existing < 0) {
                existing = fs_create_child(dst_node, g_fs_nodes[src].name, FS_NODE_FILE);
            }
            if (existing < 0 || g_fs_nodes[existing].type != FS_NODE_FILE) {
                print_str("cp: destination create failed\n");
                return;
            }
            fs_copy_file_content(existing, src);
            return;
        }
        if (g_fs_nodes[dst_node].type != FS_NODE_FILE) {
            print_str("cp: invalid destination\n");
            return;
        }
        fs_copy_file_content(dst_node, src);
        return;
    }

    int16_t parent;
    char name[FS_NAME_MAX];
    if (!fs_resolve_parent_for_create(dst_path, &parent, name)) {
        print_str("cp: invalid destination path\n");
        return;
    }
    int16_t created = fs_create_child(parent, name, FS_NODE_FILE);
    if (created < 0) {
        print_str("cp: destination create failed\n");
        return;
    }
    fs_copy_file_content(created, src);
}

void ramfs_cmd_cat(const char* path) {
    int16_t node;
    if (path == NULL || path[0] == '\0') {
        print_str("cat: missing operand\n");
        return;
    }
    if (!fs_resolve_path(path, &node)) {
        print_str("cat: file not found\n");
        return;
    }
    if (g_fs_nodes[node].type != FS_NODE_FILE) {
        print_str("cat: not a file\n");
        return;
    }
    print_str(g_fs_nodes[node].content);
    if (g_fs_nodes[node].content_len == 0 || g_fs_nodes[node].content[g_fs_nodes[node].content_len - 1] != '\n') {
        print_char('\n');
    }
}

void ramfs_cmd_rm(const char* path) {
    int16_t node;
    if (path == NULL || path[0] == '\0') {
        print_str("rm: missing operand\n");
        return;
    }
    if (!fs_resolve_path(path, &node)) {
        print_str("rm: path not found\n");
        return;
    }
    if (node == g_fs_root) {
        print_str("rm: cannot remove root\n");
        return;
    }
    if (g_fs_nodes[node].type != FS_NODE_FILE) {
        print_str("rm: not a file\n");
        return;
    }
    if (!fs_detach_child(node)) {
        print_str("rm: detach failed\n");
        return;
    }
    fs_free_node_shallow(node);
}

void ramfs_cmd_rmdir(const char* path) {
    int16_t node;
    if (path == NULL || path[0] == '\0') {
        print_str("rmdir: missing operand\n");
        return;
    }
    if (!fs_resolve_path(path, &node)) {
        print_str("rmdir: path not found\n");
        return;
    }
    if (node == g_fs_root) {
        print_str("rmdir: cannot remove root\n");
        return;
    }
    if (node == g_fs_home) {
        print_str("rmdir: cannot remove home\n");
        return;
    }
    if (g_fs_nodes[node].type != FS_NODE_DIR) {
        print_str("rmdir: not a directory\n");
        return;
    }
    if (g_fs_nodes[node].first_child >= 0) {
        print_str("rmdir: directory not empty\n");
        return;
    }
    if (fs_is_ancestor(node, g_fs_cwd)) {
        print_str("rmdir: directory is in use\n");
        return;
    }
    if (!fs_detach_child(node)) {
        print_str("rmdir: detach failed\n");
        return;
    }
    fs_free_node_shallow(node);
}

void ramfs_cmd_rm_recursive(const char* path) {
    int16_t node;
    if (path == NULL || path[0] == '\0') {
        print_str("rm: missing operand\n");
        return;
    }
    if (!fs_resolve_path(path, &node)) {
        print_str("rm: path not found\n");
        return;
    }
    if (node == g_fs_root) {
        print_str("rm: cannot remove root\n");
        return;
    }
    if (node == g_fs_home) {
        print_str("rm: cannot remove home\n");
        return;
    }
    if (!fs_remove_node_recursive_internal(node)) {
        print_str("rm: recursive remove failed\n");
    }
}

void ramfs_cmd_stat(const char* path) {
    int16_t node;
    char full_path[PATH_BUF_MAX];
    if (path == NULL || path[0] == '\0') {
        node = g_fs_cwd;
    } else if (!fs_resolve_path(path, &node)) {
        print_str("stat: path not found\n");
        return;
    }

    print_str("type=");
    if (g_fs_nodes[node].type == FS_NODE_DIR) {
        print_str("dir\n");
    } else if (g_fs_nodes[node].type == FS_NODE_FILE) {
        print_str("file\n");
    } else {
        print_str("unknown\n");
    }

    if (fs_build_path(node, full_path, sizeof(full_path))) {
        print_str("path=");
        print_str(full_path);
        print_char('\n');
    }

    if (g_fs_nodes[node].type == FS_NODE_DIR) {
        print_str("children=");
        print_uint64_dec(fs_dir_child_count(node));
        print_char('\n');
    } else if (g_fs_nodes[node].type == FS_NODE_FILE) {
        print_str("size=");
        print_uint64_dec(g_fs_nodes[node].content_len);
        print_char('\n');
    }
}

void ramfs_cmd_write(const char* path, const char* text) {
    if (path == NULL || path[0] == '\0') {
        print_str("write: missing file operand\n");
        return;
    }
    if (text == NULL) {
        text = "";
    }
    if (!ramfs_open_or_create_file(path)) {
        print_str("write: create/open failed\n");
        return;
    }
    if (!ramfs_write_file(path, text, fn_strlen(text))) {
        print_str("write: failed\n");
    }
}

void ramfs_cmd_append(const char* path, const char* text) {
    int16_t node;
    if (path == NULL || path[0] == '\0') {
        print_str("append: missing file operand\n");
        return;
    }
    if (text == NULL) {
        text = "";
    }
    if (!ramfs_open_or_create_file(path)) {
        print_str("append: create/open failed\n");
        return;
    }
    if (!fs_resolve_path(path, &node) || g_fs_nodes[node].type != FS_NODE_FILE) {
        print_str("append: invalid file\n");
        return;
    }

    size_t add_len = fn_strlen(text);
    size_t old_len = g_fs_nodes[node].content_len;
    size_t new_len = old_len + add_len;
    if (new_len >= FS_FILE_CONTENT_MAX) {
        new_len = FS_FILE_CONTENT_MAX - 1;
    }
    size_t writable = (new_len > old_len) ? (new_len - old_len) : 0;
    for (size_t i = 0; i < writable; i++) {
        g_fs_nodes[node].content[old_len + i] = text[i];
    }
    g_fs_nodes[node].content[new_len] = '\0';
    g_fs_nodes[node].content_len = (uint16_t) new_len;
}

void ramfs_cmd_grep(const char* needle, const char* path) {
    int16_t node;
    if (needle == NULL || needle[0] == '\0') {
        print_str("grep: missing pattern\n");
        return;
    }
    if (path == NULL || path[0] == '\0') {
        print_str("grep: missing file operand\n");
        return;
    }
    if (!fs_resolve_path(path, &node)) {
        print_str("grep: file not found\n");
        return;
    }
    if (g_fs_nodes[node].type != FS_NODE_FILE) {
        print_str("grep: not a file\n");
        return;
    }

    const char* data = g_fs_nodes[node].content;
    size_t len = g_fs_nodes[node].content_len;
    size_t nlen = fn_strlen(needle);
    uint8_t matched_any = 0;
    uint64_t line_no = 1;
    size_t line_start = 0;

    if (nlen == 0) {
        print_str("grep: empty pattern\n");
        return;
    }

    for (size_t i = 0; i <= len; i++) {
        if (i == len || data[i] == '\n') {
            size_t line_end = i;
            uint8_t found = 0;
            if (line_end >= line_start && (line_end - line_start) >= nlen) {
                for (size_t j = line_start; j + nlen <= line_end; j++) {
                    size_t k = 0;
                    while (k < nlen && data[j + k] == needle[k]) {
                        k++;
                    }
                    if (k == nlen) {
                        found = 1;
                        break;
                    }
                }
            }
            if (found) {
                matched_any = 1;
                print_uint64_dec(line_no);
                print_str(": ");
                for (size_t j = line_start; j < line_end; j++) {
                    print_char(data[j]);
                }
                print_char('\n');
            }
            line_no++;
            line_start = i + 1;
        }
    }

    if (!matched_any) {
        print_str("grep: no match\n");
    }
}

void ramfs_cmd_cp_recursive(const char* src_path, const char* dst_path) {
    int16_t src;
    if (src_path == NULL || dst_path == NULL || src_path[0] == '\0' || dst_path[0] == '\0') {
        print_str("cp: missing operand\n");
        return;
    }
    if (!fs_resolve_path(src_path, &src)) {
        print_str("cp: source not found\n");
        return;
    }

    if (g_fs_nodes[src].type == FS_NODE_FILE) {
        ramfs_cmd_cp(src_path, dst_path);
        return;
    }

    int16_t dst_existing;
    int16_t parent;
    char name[FS_NAME_MAX];

    if (fs_resolve_path(dst_path, &dst_existing)) {
        if (g_fs_nodes[dst_existing].type != FS_NODE_DIR) {
            print_str("cp: destination exists and is not a directory\n");
            return;
        }
        if (fs_is_ancestor(src, dst_existing)) {
            print_str("cp: cannot copy directory into itself\n");
            return;
        }
        if (fs_clone_subtree_into(src, dst_existing, g_fs_nodes[src].name) < 0) {
            print_str("cp: recursive copy failed\n");
        }
        return;
    }

    if (!fs_resolve_parent_for_create(dst_path, &parent, name)) {
        print_str("cp: invalid destination path\n");
        return;
    }
    if (fs_is_ancestor(src, parent)) {
        print_str("cp: cannot copy directory into itself\n");
        return;
    }
    if (fs_clone_subtree_into(src, parent, name) < 0) {
        print_str("cp: recursive copy failed\n");
    }
}

void ramfs_get_stats(struct RamfsStats* out_stats) {
    if (out_stats == NULL) {
        return;
    }
    fs_collect_stats(out_stats);
}
