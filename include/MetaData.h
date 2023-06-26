#pragma once
#include "types/ext4_super.h"
#include <stdint.h>
#include <fcntl.h>
#include <vector>

#define BOOT_SECTOR_SIZE 0x400

class MetaDataManager {
public:
  static MetaDataManager &get_instance();
  void super_block_fill();
  void gdt_fill();

  // super block stat
  uint32_t block_size();
  uint64_t block_to_bytes(uint64_t blks);
  uint32_t bytes_to_block(uint64_t bytes);
  uint32_t inode_size();
  uint32_t inodes_per_group();
  off_t inode_table_offset(uint32_t inode_idx);

private:
  ext4_super_block super_;
  std::vector<ext4_group_desc> gdt_table_;

  MetaDataManager() = default;

  // private super block stat
  uint64_t block_group_size();
  uint32_t block_groups_count();
  uint32_t group_desc_size();
};