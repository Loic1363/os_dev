#pragma once

#include <stddef.h>
#include <stdint.h>

void ramfs_init();
void ramfs_get_prompt_cwd(char* out, size_t out_max);
unsigned char ramfs_complete_path(const char* input_path, char* out_path, size_t out_max);
unsigned char ramfs_open_or_create_file(const char* path);
unsigned char ramfs_read_file(const char* path, char* out, size_t out_max, size_t* out_len);
unsigned char ramfs_write_file(const char* path, const char* data, size_t len);
void ramfs_cmd_pwd();
void ramfs_cmd_ls(const char* path_opt);
void ramfs_cmd_tree(const char* path_opt);
void ramfs_cmd_cd(const char* path_opt);
void ramfs_cmd_mkdir(const char* path);
void ramfs_cmd_touch(const char* path);
void ramfs_cmd_mv(const char* src_path, const char* dst_path);
void ramfs_cmd_cp(const char* src_path, const char* dst_path);
void ramfs_cmd_cat(const char* path);
void ramfs_cmd_rm(const char* path);
void ramfs_cmd_rmdir(const char* path);
void ramfs_cmd_stat(const char* path);
