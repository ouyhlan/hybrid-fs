#pragma once
#define FUSE_USE_VERSION FUSE_MAKE_VERSION(3, 15)
#include <fuse.h>

void *fs_init(fuse_conn_info *conn, fuse_config *cfg);
int fs_open(const char *path, fuse_file_info *fi);
int fs_getattr(const char *path, struct stat *, fuse_file_info *fi);
int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
            fuse_file_info *fi, fuse_readdir_flags flags);
int fs_read(const char *path, char *buf, size_t size, off_t offset,
         fuse_file_info *fi);