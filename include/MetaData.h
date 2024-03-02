#pragma once
#include "types/ext4_super.h"
#include "types/hdd_super.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <shared_mutex>
#include <stdint.h>
#include <sys/types.h>
#include <vector>

#define BOOT_SECTOR_SIZE 0x400
class MetaDataManager {
public:
  static MetaDataManager &get_instance();
  void super_block_fill();
  void gdt_fill();

  // hdd disk
  void hdd_disk_init();

  // super block stat
  uint32_t block_size();
  uint64_t block_to_bytes(uint64_t blks);
  uint32_t bytes_to_block(uint64_t bytes);
  uint32_t inode_size();
  uint32_t inodes_per_group();
  uint32_t blocks_per_group();

  // offset
  off_t inode_table_offset(uint32_t inode_idx);
  off_t inode_table_entry_offset(uint32_t inode_idx);

  // return new ssd block idx
  uint32_t alloc_new_pblock(uint32_t lblock);
  uint32_t alloc_new_ssd_pblock();
  uint32_t alloc_new_hdd_pblock();
  uint32_t get_new_inode_idx();
  void free_pblock(const std::vector<uint32_t> &pblock_vec);
  void free_inode(uint32_t inode_idx);

  // stat
  void log_hdd_stat();

private:
  ext4_super_block super_;
  std::vector<ext4_group_desc> gdt_table_;

  hdd_super_block hdd_super_;
  std::vector<hdd_group_desc> hdd_gdt_table_;
  
  mutable std::shared_mutex ssd_mutex_;
  mutable std::shared_mutex hdd_mutex_;

  MetaDataManager() = default;

  // private super block stat
  uint32_t block_groups_count();
  uint64_t block_group_size();
  uint32_t group_desc_size();
  
  // private gdt stat
  off_t gdt_table_entry_offset(uint32_t group_idx);
  uint64_t get_block_bitmap_free_block_count(uint32_t group_idx);
  uint64_t get_inode_bitmap_free_block_count(uint32_t group_idx);
  void inc_block_bitmap_free_block_count(uint32_t group_idx);
  void set_block_bitmap_free_block_count(uint32_t group_idx, uint64_t free_block_count);
  void set_inode_bitmap_free_block_count(uint32_t group_idx, uint64_t free_block_count);
  uint64_t block_bitmap_block_idx(uint32_t group_idx);
  uint64_t inode_bitmap_block_idx(uint32_t group_idx);

  
};