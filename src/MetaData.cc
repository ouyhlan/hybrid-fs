#include "MetaData.h"
#include "bitmap.h"
#include "common.h"
#include "disk.h"
#include "option.h"
#include "types/ext4_inode.h"
#include "types/hdd_super.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <glog/logging.h>
#include <shared_mutex>
#include <sys/types.h>
#include <vector>

#define GROUP_DESC_MIN_SIZE 0x20

MetaDataManager &MetaDataManager::get_instance() {
  static MetaDataManager instance;
  return instance;
}

void MetaDataManager::super_block_fill() {
  GET_INSTANCE(DiskManager)
      .metadata_read(&super_, sizeof(ext4_super_block), BOOT_SECTOR_SIZE);
  GET_INSTANCE(DiskManager).set_disk_block_size(block_size());

  LOG(INFO) << "Block size: " << block_size();
  LOG(INFO) << "Group total size: " << block_group_size();
  LOG(INFO) << "Number of block groups: " << block_groups_count();
  LOG(INFO) << "Inode size: " << inode_size();
  LOG(INFO) << "Inodes per group: " << inodes_per_group();
  LOG(INFO) << "Blocks per group: " << blocks_per_group();
}

void MetaDataManager::gdt_fill() {
  uint32_t group_num = block_groups_count();
  gdt_table_.resize(group_num);

  for (uint32_t i = 0; i < group_num; i++) {
    GET_INSTANCE(DiskManager)
        .metadata_read(&(gdt_table_.data()[i]), group_desc_size(),
                       gdt_table_entry_offset(i));
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

uint32_t MetaDataManager::blocks_per_group() {
  return super_.s_blocks_per_group;
}

// assume inode_idx > 0
off_t MetaDataManager::inode_table_offset(uint32_t inode_idx) {
  assert(inode_idx > 0);

  uint32_t n = inode_idx - 1; // Inode 0 doesn't exist on disk
  uint32_t group_idx = n / inodes_per_group();
  assert(group_idx < block_groups_count());

  LOG(INFO) << "Inode idx: #" << n + 1 << " 's Inode table offset: "
            << gdt_table_[group_idx].bg_inode_table_lo;
  return block_to_bytes(gdt_table_[group_idx].bg_inode_table_lo);
}

off_t MetaDataManager::inode_table_entry_offset(uint32_t inode_idx) {
  assert(inode_idx > 0);

  uint32_t n = inode_idx - 1; // Inode 0 doesn't exist on disk
  uint32_t idx_in_group = n % inodes_per_group();

  return inode_table_offset(n) + idx_in_group * inode_size();
}

uint64_t
MetaDataManager::get_block_bitmap_free_block_count(uint32_t group_idx) {
  assert(group_idx < block_groups_count());
  return gdt_table_[group_idx].bg_free_blocks_count_lo;
}

uint64_t
MetaDataManager::get_inode_bitmap_free_block_count(uint32_t group_idx) {
  assert(group_idx < block_groups_count());
  return gdt_table_[group_idx].bg_free_inodes_count_lo;
}

void MetaDataManager::set_block_bitmap_free_block_count(
    uint32_t group_idx, uint64_t free_block_count) {
  assert(group_idx < block_groups_count());
  gdt_table_[group_idx].bg_free_blocks_count_lo = free_block_count;
}

void MetaDataManager::inc_block_bitmap_free_block_count(uint32_t group_idx) {
  assert(group_idx < block_groups_count());
  gdt_table_[group_idx].bg_free_blocks_count_lo++;
}

void MetaDataManager::set_inode_bitmap_free_block_count(
    uint32_t group_idx, uint64_t free_block_count) {
  assert(group_idx < block_groups_count());
  gdt_table_[group_idx].bg_free_inodes_count_lo = free_block_count;
}

uint64_t MetaDataManager::block_bitmap_block_idx(uint32_t group_idx) {
  assert(group_idx < block_groups_count());
  return gdt_table_[group_idx].bg_block_bitmap_lo;
}

uint64_t MetaDataManager::inode_bitmap_block_idx(uint32_t group_idx) {
  assert(group_idx < block_groups_count());
  return gdt_table_[group_idx].bg_inode_bitmap_lo;
}

off_t MetaDataManager::gdt_table_entry_offset(uint32_t group_idx) {
  assert(group_idx < block_groups_count());
  off_t gdt_off = BOOT_SECTOR_SIZE + sizeof(ext4_super_block);
  gdt_off = ALIGN_TO(gdt_off, block_size()) + group_idx * group_desc_size();
  return gdt_off;
}

uint32_t MetaDataManager::alloc_new_pblock(uint32_t lblock) {
  if (lblock < SSD_MAX_LBLOCK) {
    return alloc_new_ssd_pblock();
  } else {
    return alloc_new_hdd_pblock();
  }
}

uint32_t MetaDataManager::alloc_new_ssd_pblock() {
  assert(block_size() % sizeof(uint32_t) == 0);

  std::shared_lock lock(ssd_mutex_);
  for (uint32_t group_id = 0; group_id < block_groups_count(); group_id++) {
    uint64_t free_block_count = get_block_bitmap_free_block_count(group_id);
    if (free_block_count > 0) {
      uint64_t bitmap_pblock = block_bitmap_block_idx(group_id);
      Bitmap bitmap(block_size());
      bitmap.load(bitmap_pblock);

      // LOG(INFO) << "SSD group #" << group_id << " found " << free_block_count
      //           << " free block, block bitmap idx: " << bitmap_pblock;

      uint32_t idx = 0;
      uint32_t begin_idx = (group_id == 0) ? 1 : 0;
      for (uint32_t i = begin_idx; i < bitmap.size(); i++) {
        if (!bitmap.lookup(i)) {
          idx = i;
          break;
        }
      }

      // set idx's bitmap
      bitmap.set(idx);
      bitmap.save(bitmap_pblock);

      // update gdt
      set_block_bitmap_free_block_count(group_id, free_block_count - 1);
      GET_INSTANCE(DiskManager)
          .metadata_write(&(gdt_table_.data()[group_id]), group_desc_size(),
                          gdt_table_entry_offset(group_id));

      uint32_t alloc_pblock = group_id * blocks_per_group() + idx;
      // LOG(INFO) << "SSD return new free block idx: " << alloc_pblock;
      return alloc_pblock;
    }
  }
  LOG(FATAL) << "SSD no free blocks!";
  return 0;
}

uint32_t MetaDataManager::get_new_inode_idx() {
  assert(block_size() % sizeof(uint32_t) == 0);

  std::shared_lock lock(ssd_mutex_);
  for (uint32_t group_id = 0; group_id < block_groups_count(); group_id++) {
    uint64_t free_inode_count = get_inode_bitmap_free_block_count(group_id);
    if (free_inode_count > 0) {
      uint64_t bitmap_pblock = inode_bitmap_block_idx(group_id);
      Bitmap bitmap(block_size());
      bitmap.load(bitmap_pblock);

      LOG(INFO) << "Group #" << group_id << " found " << free_inode_count
                << " inodes, inode bitmap pblock: " << bitmap_pblock;

      uint32_t idx = 0;
      uint32_t begin_idx = (group_id == 0) ? 11 : 0;
      for (uint32_t i = begin_idx; i < bitmap.size(); i++) {
        if (!bitmap.lookup(i)) {
          idx = i;
          break;
        }
      }

      // set and update inode's bitmap
      bitmap.set(idx);
      bitmap.save(bitmap_pblock);

      // update gdt
      set_inode_bitmap_free_block_count(group_id, free_inode_count - 1);
      GET_INSTANCE(DiskManager)
          .metadata_write(&(gdt_table_.data()[group_id]), group_desc_size(),
                          gdt_table_entry_offset(group_id));

      // inode numbers start from 1 rather than 0
      uint32_t alloc_inode_idx = group_id * inodes_per_group() + idx + 1;
      LOG(INFO) << "Allocate inode: " << alloc_inode_idx;
      return alloc_inode_idx;
    }
  }
  LOG(FATAL) << "No free inodes!";
  return 0;
}

uint32_t MetaDataManager::group_desc_size() {
  if (!super_.s_desc_size)
    return GROUP_DESC_MIN_SIZE;
  else
    return sizeof(struct ext4_group_desc);
}

uint32_t MetaDataManager::alloc_new_hdd_pblock() {
  std::shared_lock lock(hdd_mutex_);

  uint32_t hdd_group_count = hdd_super_.s_group_count;
  for (uint32_t group_id = 0; group_id < hdd_group_count; group_id++) {
    uint64_t free_block_count = hdd_gdt_table_[group_id].bg_free_blocks_count;
    if (free_block_count > 0) {
      uint64_t bitmap_pblock =
          HDD_BLOCK_IDX(hdd_gdt_table_[group_id].bg_block_bitmap);
      Bitmap bitmap(block_size());
      bitmap.load(bitmap_pblock);

      // LOG(INFO) << "HDD group #" << group_id << " found " << free_block_count
      //           << " free block, block bitmap idx: " << (bitmap_pblock &
      //           (~HDD_MASK));

      uint32_t idx = 0;
      for (uint32_t i = 0; i < bitmap.size(); i++) {
        if (!bitmap.lookup(i)) {
          idx = i;
          break;
        }
      }

      // set idx's bitmap
      bitmap.set(idx);
      bitmap.save(bitmap_pblock);

      // update gdt
      uint32_t hdd_metadata_pblock = HDD_BLOCK_IDX(0);
      hdd_gdt_table_[group_id].bg_free_blocks_count = free_block_count - 1;
      size_t nbyte = sizeof(hdd_group_desc);
      off_t offset =
          sizeof(hdd_super_block) + group_id * sizeof(hdd_group_desc);
      GET_INSTANCE(DiskManager)
          .disk_write(&(hdd_gdt_table_.data()[group_id]), nbyte,
                      hdd_metadata_pblock, offset);

      uint32_t alloc_pblock = HDD_BLOCK_IDX(group_id * hdd_group_count + idx);
      // LOG(INFO) << "HDD return new free block idx: " << (alloc_pblock &
      // (~HDD_MASK));
      return alloc_pblock;
    }
  }
  LOG(FATAL) << "HDD no free blocks!";
  return 0;
}

void MetaDataManager::free_inode(uint32_t inode_idx) {
  assert(inode_idx > 0);
  std::shared_lock ssd_lock(ssd_mutex_);

  uint32_t n = inode_idx - 1;
  uint32_t group_id = n / inodes_per_group();
  uint64_t bitmap_pblock = inode_bitmap_block_idx(group_id);
  uint64_t free_inode_count = get_inode_bitmap_free_block_count(group_id);
  Bitmap bitmap(block_size());
  bitmap.load(bitmap_pblock);

  // set and update inode's bitmap
  bitmap.unset(n % inodes_per_group());
  bitmap.save(bitmap_pblock);

  // update gdt
  set_inode_bitmap_free_block_count(group_id, free_inode_count - 1);
  GET_INSTANCE(DiskManager)
      .metadata_write(&(gdt_table_.data()[group_id]), group_desc_size(),
                      gdt_table_entry_offset(group_id));
}

void MetaDataManager::free_pblock(const std::vector<uint32_t> &pblock_vec) {
  std::shared_lock ssd_lock(ssd_mutex_);
  std::shared_lock hdd_lock(hdd_mutex_);

  std::vector<bool> ssd_dirty(block_groups_count(), false);
  std::vector<bool> hdd_dirty(hdd_super_.s_group_count, false);
  std::vector<Bitmap> ssd_bitmap_vec(block_groups_count(), block_size());
  std::vector<Bitmap> hdd_bitmap_vec(hdd_super_.s_group_count, block_size());

  for (uint32_t i = 0; i < block_groups_count(); i++) {
    uint64_t bitmap_pblock = block_bitmap_block_idx(i);
    ssd_bitmap_vec[i].load(bitmap_pblock);
  }

  for (uint32_t i = 0; i < hdd_super_.s_group_count; i++) {
    uint64_t bitmap_pblock = HDD_BLOCK_IDX(hdd_gdt_table_[i].bg_block_bitmap);
    ssd_bitmap_vec[i].load(bitmap_pblock);
  }

  void *buf = new std::byte[block_size()];
  memset(buf, 0, block_size());
  uint32_t bitmap_size = block_size() * sizeof(uint32_t) * 8;
  for (auto &pblock : pblock_vec) {
    uint32_t group_id = (pblock & (~HDD_MASK)) / bitmap_size;
    uint32_t idx = (pblock & (~HDD_MASK)) % bitmap_size;

    if ((pblock & HDD_MASK) != 0) {
      hdd_dirty[group_id] = true;
      hdd_bitmap_vec[group_id].unset(idx);
      hdd_gdt_table_[group_id].bg_free_blocks_count++;
    } else {
      ssd_dirty[group_id] = true;
      ssd_bitmap_vec[group_id].unset(idx);
      inc_block_bitmap_free_block_count(group_id);
    }

    // clean block
    GET_INSTANCE(DiskManager).disk_block_write(buf, pblock);
  }

  for (uint32_t i = 0; i < block_groups_count(); i++) {
    if (ssd_dirty[i]) {
      uint64_t bitmap_pblock = block_bitmap_block_idx(i);
      ssd_bitmap_vec[i].save(bitmap_pblock);
      GET_INSTANCE(DiskManager)
          .metadata_write(&(gdt_table_.data()[i]), group_desc_size(),
                          gdt_table_entry_offset(i));
    }
  }

  for (uint32_t i = 0; i < hdd_super_.s_group_count; i++) {
    if (hdd_dirty[i]) {
      uint64_t bitmap_pblock = HDD_BLOCK_IDX(hdd_gdt_table_[i].bg_block_bitmap);
      ssd_bitmap_vec[i].save(bitmap_pblock);
    }
  }

  // update hdd gdt
  uint32_t hdd_metadata_pblock = HDD_BLOCK_IDX(0);
  size_t nbyte = hdd_super_.s_group_count * sizeof(hdd_group_desc);
  GET_INSTANCE(DiskManager)
      .disk_write(hdd_gdt_table_.data(), nbyte, hdd_metadata_pblock,
                  sizeof(hdd_super_block));
}

void MetaDataManager::hdd_disk_init() {
  uint32_t hdd_metadata_pblock = HDD_BLOCK_IDX(0);
  GET_INSTANCE(DiskManager)
      .disk_read(&hdd_super_, sizeof(hdd_super_block), hdd_metadata_pblock, 0);

  // initialize hdd group
  if (hdd_super_.s_group_count == 0) {
    // blocks per group = block_size() * 8
    uint32_t hdd_blocks_per_group = block_size() * 8;
    uint32_t hdd_group_count =
        hdd_super_.s_file_size / (hdd_blocks_per_group * block_size());

    // update hdd metadata
    hdd_super_.s_group_count = hdd_group_count;
    hdd_gdt_table_.resize(hdd_super_.s_group_count);

    uint32_t hdd_gdt_blocks_count =
        1 + (sizeof(hdd_super_block) +
             hdd_group_count * sizeof(hdd_group_desc) - 1) /
                block_size();

    // Initialize bitmap
    uint32_t hdd_bitmap_pblock;
    Bitmap bitmap(block_size());
    bitmap.set(0);

    // Setup other group
    for (uint32_t group_id = 1; group_id < hdd_super_.s_group_count;
         group_id++) {
      hdd_bitmap_pblock = HDD_BLOCK_IDX(hdd_blocks_per_group * group_id);
      hdd_gdt_table_[group_id] = {hdd_blocks_per_group - 1, hdd_bitmap_pblock};

      // setup bitmap
      bitmap.save(hdd_bitmap_pblock);
    }

    // set first group
    hdd_bitmap_pblock = HDD_BLOCK_IDX(hdd_gdt_blocks_count);
    hdd_gdt_table_[0] = {hdd_blocks_per_group - hdd_gdt_blocks_count - 1,
                         hdd_bitmap_pblock};

    // setup first group bitmap
    for (uint32_t i = 0; i <= hdd_gdt_blocks_count; i++) {
      bitmap.set(i);
    }
    bitmap.save(hdd_bitmap_pblock);

    // update hdd disk content
    GET_INSTANCE(DiskManager)
        .disk_write(&hdd_super_, sizeof(hdd_super_block), hdd_metadata_pblock,
                    0);

    // update hdd gdt
    size_t nbyte = hdd_group_count * sizeof(hdd_group_desc);
    GET_INSTANCE(DiskManager)
        .disk_write(hdd_gdt_table_.data(), nbyte, hdd_metadata_pblock,
                    sizeof(hdd_super_block));
  } else {
    uint32_t hdd_group_count = hdd_super_.s_group_count;
    hdd_gdt_table_.resize(hdd_group_count);

    size_t nbyte = hdd_group_count * sizeof(hdd_group_desc);
    GET_INSTANCE(DiskManager)
        .disk_read(hdd_gdt_table_.data(), nbyte, hdd_metadata_pblock,
                   sizeof(hdd_super_block));
  }

  LOG(INFO) << "Hdd metadata:";
  LOG(INFO) << "hdd_file_size: " << hdd_super_.s_file_size;
  LOG(INFO) << "hdd_group_count: " << hdd_super_.s_group_count;
}

void MetaDataManager::log_hdd_stat() {
  uint32_t block_count = hdd_super_.s_file_size / block_size();
  for (uint32_t i = 0; i < hdd_gdt_table_.size(); i++) {
    block_count -= hdd_gdt_table_[i].bg_free_blocks_count;
  }
  LOG(INFO) << "HDD occupy " << block_count << " blocks";
}