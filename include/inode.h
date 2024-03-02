#pragma once
#include "MetaData.h"
#include "bitmap.h"
#include "common.h"
#include "types/ext4_dentry.h"
#include "types/ext4_inode.h"
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <string>
#include <vector>

struct DirCtx {
  uint32_t lblock;
  std::byte *buf;

  DirCtx(uint32_t block_size) : lblock(-1) { buf = new std::byte[block_size]; }
  ~DirCtx() { delete[] buf; }
};

struct InodeCtx {
  bool dirty;
  ext4_inode inode;
};

class InodeManager {
public:
  static InodeManager &get_instance();
  int init();
  int get_inode_by_path(const std::string &path, ext4_inode &inode);
  int get_inode_by_idx(uint32_t n, ext4_inode &res);
  ext4_dir_entry_2 *get_dentry(const ext4_inode &inode, off_t offset,
                               DirCtx &ctx);
  uint32_t get_idx_by_path(const std::string &path);

  // file stat
  uint64_t get_file_size(const ext4_inode &inode);
  void set_file_size(ext4_inode &inode, uint64_t new_size);
  uint32_t get_file_blocks_count(const ext4_inode &inode);
  void set_file_blocks_count(ext4_inode &inode, uint32_t new_blocks_count);

  // file datablock
  uint32_t get_data_pblock(const ext4_inode &inode, uint32_t lblock);
  void set_data_pblock(ext4_inode &inode, uint32_t lblock, uint32_t pblock);
  void collect_file_pblock(ext4_inode &inode, std::vector<uint32_t> &pblock_vec);

  // create file
  void add_dentry(ext4_inode &prefix_inode, const ext4_dir_entry_2 &new_dentry);
  void update_disk_inode(uint32_t inode_idx, const ext4_inode &inode);

  // delete file
  void rm_dentry(ext4_inode &prefix_inode, uint32_t cur_inode_idx);
  void rm_dir(ext4_inode &cur_inode, uint32_t cur_inode_idx);
  void rm_file(ext4_inode &cur_inode, uint32_t cur_inode_idx);

private:
  uint32_t block_size_;
  size_t inode_size_;
  InodeManager() = default;

  // data block function
  uint32_t get_data_pblock_ind(uint32_t lblock, uint32_t index_block);
  uint32_t get_data_pblock_dind(uint32_t lblock, uint32_t dindex_block);
  uint32_t get_data_pblock_tind(uint32_t lblock, uint32_t tindex_block);
  void set_data_lblock_ind(uint32_t lblock, uint32_t index_block,
                           uint32_t pblock);
  void set_data_lblock_dind(uint32_t lblock, uint32_t dindex_block,
                            uint32_t pblock);
  void set_data_lblock_tind(uint32_t lblock, uint32_t tindex_block,
                            uint32_t pblock);
  void collect_file_pblock_ind(uint32_t index_block, std::vector<uint32_t> &pblock_vec);
  void collect_file_pblock_dind(uint32_t dindex_block, std::vector<uint32_t> &pblock_vec);
  void collect_file_pblock_tind(uint32_t tindex_block, std::vector<uint32_t> &pblock_vec);
};