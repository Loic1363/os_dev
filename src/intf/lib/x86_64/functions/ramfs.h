#pragma once

#include <stddef.h>

void ramfs_init();
void ramfs_get_prompt_cwd(char* out, size_t out_max);
unsigned char ramfs_complete_path(const char* input_path, char* out_path, size_t out_max);
void ramfs_cmd_pwd();
void ramfs_cmd_ls(const char* path_opt);
void ramfs_cmd_tree(const char* path_opt);
void ramfs_cmd_cd(const char* path_opt);
void ramfs_cmd_mkdir(const char* path);
void ramfs_cmd_touch(const char* path);
void ramfs_cmd_mv(const char* src_path, const char* dst_path);
