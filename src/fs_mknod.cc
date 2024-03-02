#include "ops.h"
#include "common.h"
#include "inode.h"
#include "types/ext4_inode.h"
#include <glog/logging.h>

int fs_mknod(const char *path_cstr, mode_t mode, dev_t rdev) {
  LOG(INFO) << "Mknod begin:";
  LOG(INFO) << "mknod( " << path_cstr << ", " << mode << " , " << rdev << " )";
  std::string parent_path, filename;

  // get path's parent directory
  // parent directory does exist ensure by the callee
  get_parent_dir(path_cstr, parent_path, filename);
  LOG(INFO) << "parent directory: " << parent_path << " dirname: " << filename;

  if (filename.size() > EXT4_NAME_LEN)
    return -ENAMETOOLONG;

  int ret;
  uint32_t parent_inode_idx, cur_inode_idx;
  ext4_inode prefix_inode, cur_inode;
  parent_inode_idx = GET_INSTANCE(InodeManager).get_idx_by_path(parent_path);
  GET_INSTANCE(InodeManager).get_inode_by_idx(parent_inode_idx, prefix_inode);

  // Create new file
  // Allocate inode for new file
  cur_inode_idx = GET_INSTANCE(MetaDataManager).get_new_inode_idx();

  // Initialize new file inode
  memset(&cur_inode, 0, sizeof(ext4_inode));
  uint16_t i_mode = mode;
  cur_inode = {
      .i_mode = i_mode,
  };

  // Update on-disk parent_inode file content
  ext4_dir_entry_2 cur_dentry;
  set_dir_dentry(cur_dentry, cur_inode_idx, filename, 0x1);
  GET_INSTANCE(InodeManager).add_dentry(prefix_inode, cur_dentry);

  // Update on-disk Inode table
  GET_INSTANCE(InodeManager).update_disk_inode(cur_inode_idx, cur_inode);
  GET_INSTANCE(InodeManager).update_disk_inode(parent_inode_idx, prefix_inode);

  LOG(INFO) << "Mknod done";
  return 0;
}