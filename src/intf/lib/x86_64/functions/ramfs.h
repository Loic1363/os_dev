#pragma once

void ramfs_init();
void ramfs_cmd_pwd();
void ramfs_cmd_ls(const char* path_opt);
void ramfs_cmd_tree(const char* path_opt);
void ramfs_cmd_cd(const char* path_opt);
void ramfs_cmd_mkdir(const char* path);
void ramfs_cmd_touch(const char* path);
void ramfs_cmd_mv(const char* src_path, const char* dst_path);
