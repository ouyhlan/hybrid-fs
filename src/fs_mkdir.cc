#include "ops.h"
#include "MetaData.h"
#include "common.h"
#include "inode.h"
#include "types/ext4_dentry.h"
#include "types/ext4_inode.h"
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <glog/logging.h>
#include <string>

// Only call this function when it is really want to create a new directory
int fs_mkdir(const char *path_cstr, mode_t mode) {
  LOG(INFO) << "Mkdir begin:";
  LOG(INFO) << "New directory: " << path_cstr;
  std::string parent_path, dirname;

  // get path's parent directory
  // parent directory does exist ensure by the callee
  get_parent_dir(path_cstr, parent_path, dirname);
  LOG(INFO) << "parent directory: " << parent_path << " dirname: " << dirname;

  if (dirname.size() > EXT4_NAME_LEN)
    return -ENAMETOOLONG;

  uint32_t parent_inode_idx, cur_inode_idx;
  ext4_inode prefix_inode, cur_inode;
  parent_inode_idx = GET_INSTANCE(InodeManager).get_idx_by_path(parent_path);
  GET_INSTANCE(InodeManager).get_inode_by_idx(parent_inode_idx, prefix_inode);

  // Create new directory file
  // Allocate inode for new directory
  cur_inode_idx = GET_INSTANCE(MetaDataManager).get_new_inode_idx();

  // Initialize new dir inode
  memset(&cur_inode, 0, sizeof(ext4_inode));
  uint16_t i_mode = mode | __S_IFDIR;
  cur_inode = {
      .i_mode = i_mode,
  };

  // Initialize on-disk cur_inode file content
  ext4_dir_entry_2 dot, dotdot;
  set_dir_dentry(dot, cur_inode_idx, ".", 0x2);
  set_dir_dentry(dotdot, parent_inode_idx, "..", 0x2);
  GET_INSTANCE(InodeManager).add_dentry(cur_inode, dot);
  GET_INSTANCE(InodeManager).add_dentry(cur_inode, dotdot);

  // Update on-disk parent_inode file content
  ext4_dir_entry_2 cur_dentry;
  set_dir_dentry(cur_dentry, cur_inode_idx, dirname, 0x2);
  GET_INSTANCE(InodeManager).add_dentry(prefix_inode, cur_dentry);

  // Update on-disk Inode table
  GET_INSTANCE(InodeManager).update_disk_inode(cur_inode_idx, cur_inode);
  GET_INSTANCE(InodeManager).update_disk_inode(parent_inode_idx, prefix_inode);

  LOG(INFO) << "Mkdir done";
  return 0;
}