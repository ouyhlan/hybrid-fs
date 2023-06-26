#pragma once
#include "types/ext4_dentry.h"
#include "types/ext4_inode.h"
#include <cstddef>
#include <cstdint>
#include <fcntl.h>

struct DirCtx {
  uint32_t lblock;
  std::byte *buf;

  DirCtx(uint32_t block_size) : lblock(-1) { buf = new std::byte[block_size]; }
  ~DirCtx() { delete[] buf; }
};

class InodeManager {
public:
  static InodeManager &get_instance();
  int init();
  int get_inode_by_path(const char *path, ext4_inode &inode);
  int get_inode_by_idx(uint32_t n, ext4_inode &res);
  ext4_dir_entry_2 *get_dentry(const ext4_inode &inode, off_t offset,
                               DirCtx &ctx);
  uint32_t get_idx_by_path(const char *path);

  // file stat
  uint64_t get_file_size(const ext4_inode &inode);
  uint32_t get_data_pblock(const ext4_inode &inode, uint32_t lblock);

private:
  uint32_t block_size_;
  InodeManager() = default;

  // data block function
  uint32_t get_data_pblock_ind(uint32_t lblock, uint32_t index_block);
  uint32_t get_data_pblock_dind(uint32_t lblock, uint32_t dindex_block);
  uint32_t get_data_pblock_tind(uint32_t lblock, uint32_t tindex_block);
};