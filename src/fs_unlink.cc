#include "ops.h"
#include "MetaData.h"
#include "common.h"
#include "inode.h"
#include <cstdint>
#include <glog/logging.h>
#include <vector>

int fs_unlink(const char *path) {
  LOG(INFO) << "Unlink begin:";
  LOG(INFO) << "unlink( " << path  << " )";
  std::string parent_path, filename;

  // get path's parent directory
  // parent directory does exist ensure by the callee
  get_parent_dir(path, parent_path, filename);
  LOG(INFO) << "parent directory: " << parent_path << " filename: " << filename;

  uint32_t parent_inode_idx, cur_inode_idx;
  ext4_inode prefix_inode, cur_inode;
  cur_inode_idx = GET_INSTANCE(InodeManager).get_idx_by_path(path);
  GET_INSTANCE(InodeManager).get_inode_by_idx(cur_inode_idx, cur_inode);
  GET_INSTANCE(InodeManager).get_inode_by_path(parent_path, prefix_inode);

  GET_INSTANCE(InodeManager).rm_dentry(prefix_inode, cur_inode_idx);
  GET_INSTANCE(InodeManager).rm_file(cur_inode, cur_inode_idx);

  LOG(INFO) << "Unlink done";
  return 0;
}