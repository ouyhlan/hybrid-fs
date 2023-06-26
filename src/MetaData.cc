#include "MetaData.h"
#include "common.h"
#include "disk.h"

#include <cassert>
#include <cstdint>
#include <glog/logging.h>
#include <sys/types.h>

#define GROUP_DESC_MIN_SIZE 0x20

MetaDataManager &MetaDataManager::get_instance() {
  static MetaDataManager instance;
  return instance;
}

void MetaDataManager::super_block_fill() {
  GET_INSTANCE(DiskManager)
      .ssd_disk_read(&super_, sizeof(ext4_super_block), BOOT_SECTOR_SIZE);

  LOG(INFO) << "Block size: " << block_size();
  LOG(INFO) << "Group total size: " << block_group_size();
  LOG(INFO) << "Number of block groups: " << block_groups_count();
  LOG(INFO) << "Inode size: " << inode_size();
  LOG(INFO) << "Inodes per group: " << inodes_per_group();
}

void MetaDataManager::gdt_fill() {
  uint32_t group_num = block_groups_count();
  gdt_table_.resize(group_num);

  for (uint32_t i = 0; i < group_num; i++) {
    // set the beginning of gdt
    off_t gdt_off = BOOT_SECTOR_SIZE + sizeof(ext4_super_block);
    gdt_off = ALIGN_TO(gdt_off, block_size()) + i * group_desc_size();

    GET_INSTANCE(DiskManager)
        .ssd_disk_read(&(gdt_table_.data()[i]), group_desc_size(), gdt_off);
  }
}

// Block size is 2 ^ (10 + s_log_block_size)
uint32_t MetaDataManager::block_size() {
  return ((uint64_t)1) << (super_.s_log_block_size + 10);
}

uint64_t MetaDataManager::block_to_bytes(uint64_t blks) {
  return blks * block_size();
}

uint32_t MetaDataManager::bytes_to_block(uint64_t bytes) {
  return (bytes / block_size()) + ((bytes % block_size()) ? 1 : 0);
}

uint64_t MetaDataManager::block_group_size() {
  return block_to_bytes(super_.s_blocks_per_group);
}

uint32_t MetaDataManager::block_groups_count() {
  uint32_t n = (super_.s_blocks_count_lo + super_.s_blocks_per_group - 1) /
               super_.s_blocks_per_group;
  return n ? n : 1;
}

uint32_t MetaDataManager::inode_size() { return super_.s_inode_size; }

uint32_t MetaDataManager::inodes_per_group() {
  return super_.s_inodes_per_group;
}

off_t MetaDataManager::inode_table_offset(uint32_t n) {
  uint32_t group_idx = n / inodes_per_group();
  assert(group_idx < block_groups_count());

  LOG(INFO) << "Inode idx: #" << n + 1 << " 's Inode table offset: "
             << gdt_table_[group_idx].bg_inode_table_lo;
  return block_to_bytes(gdt_table_[group_idx].bg_inode_table_lo);
}

uint32_t MetaDataManager::group_desc_size() {
  if (!super_.s_desc_size)
    return GROUP_DESC_MIN_SIZE;
  else
    return sizeof(struct ext4_group_desc);
}