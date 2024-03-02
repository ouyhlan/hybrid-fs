#include "ops.h"
#include "common.h"
#include "inode.h"
#include "types/ext4_inode.h"
#include <glog/logging.h>
#include <regex>

int fs_getattr(const char *path, struct stat *stbuf, fuse_file_info *fi) {
  LOG(INFO) << "Getattr begin:";
  
  ext4_inode inode;
  
  int ret;
  if (fi) {
    ret = GET_INSTANCE(InodeManager).get_inode_by_idx(fi->fh, inode);
  } else {
    ret = GET_INSTANCE(InodeManager).get_inode_by_path(path, inode);
  }

  if (ret < 0) {
    return ret;
  }

  stbuf->st_mode = inode.i_mode;
  stbuf->st_nlink = inode.i_links_count;
  stbuf->st_size = GET_INSTANCE(InodeManager).get_file_size(inode);
  stbuf->st_blocks = inode.i_blocks_lo;
  stbuf->st_uid = inode.i_uid;
  stbuf->st_gid = inode.i_gid;
  stbuf->st_atime = inode.i_atime;
  stbuf->st_mtime = inode.i_mtime;
  stbuf->st_ctime = inode.i_ctime;

  LOG(INFO) << path << " 's stat: mode=" << stbuf->st_mode << " size=" << stbuf->st_size << " blocks=" << stbuf->st_blocks;
  LOG(INFO) << "Getattr done!";
  return 0;
}