#include "ops.h"
#include "common.h"
#include "inode.h"
#include <cstdint>
#include <glog/logging.h>

int fs_open(const char *path, fuse_file_info *fi) {
  LOG(INFO) << "Open begin:";
  LOG(INFO) << "Open file: " << path;

  uint32_t inode_num = GET_INSTANCE(InodeManager).get_idx_by_path(path);
  if (inode_num == 0)
    return -ENOENT;
  fi->fh = inode_num;
  LOG(INFO) << "Open " << path << " in inode: #" << fi->fh;
  LOG(INFO) << "Open done";
  return 0;
}