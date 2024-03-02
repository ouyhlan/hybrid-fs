#pragma once

#define ALIGN_TO(__n, __align)                                                 \
  ({                                                                           \
    typeof(__n) __ret;                                                         \
    if ((__n) % (__align)) {                                                   \
      __ret = ((__n) & (~((__align)-1))) + (__align);                          \
    } else                                                                     \
      __ret = (__n);                                                           \
    __ret;                                                                     \
  })

#define GET_INSTANCE(X) X::get_instance()

#include "types/ext4_dentry.h"
#include "types/ext4_inode.h"
#include <cassert>
#include <cstring>
#include <string>

inline void get_parent_dir(const std::string &path, std::string &parent,
                           std::string &filename) {
  assert(path.size() > 0 && path[0] == '/');
  size_t npos = path.size() - 1;

  // ignore the last '/'
  if (path[npos] == '/')
    npos--;

  // find parent prefix
  while (npos >= 0 && path[npos] != '/') {
    npos--;
  }

  // path[npos] = "/"
  // prefix length = npos + 1
  parent = path.substr(0, npos + 1);
  filename = path.substr(npos + 1);
}

inline void set_dir_dentry(ext4_dir_entry_2 &dentry, uint32_t inode_idx,
                           const std::string &filename, uint8_t file_type) {
  uint16_t rec_len = sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint8_t) * 2 +
                     filename.size();

  dentry.inode = inode_idx;
  dentry.rec_len = ALIGN_TO(rec_len, sizeof(uint32_t));
  dentry.name_len = filename.size();
  dentry.file_type = file_type;

  memset(dentry.name, 0, EXT4_NAME_LEN);
  for (size_t i = 0; i < filename.size(); i++) {
    dentry.name[i] = filename[i];
  }
}

inline std::string dentry_str(const ext4_dir_entry_2 &dentry) {
  std::string res =
      "dentry{ inode= " + std::to_string(dentry.inode) +
      ", rec_len=" + std::to_string((uint32_t)dentry.rec_len) +
      ", name_len=" + std::to_string((uint32_t)dentry.name_len) +
      ", file_type=" + std::to_string((uint32_t)dentry.file_type) +
      ", name=" + dentry.name + "}";
  return res;
}

inline std::string inode_str(const ext4_inode &inode) {
  std::string res = "Inode{ i_mode=" + std::to_string(inode.i_mode) +
  " i_size_lo=" + std::to_string(inode.i_size_lo) +
  " i_blocks_lo=" + std::to_string(inode.i_blocks_lo) +
  " i_block[0]=" + std::to_string(inode.i_block[0]) + " }";
  return res;
}