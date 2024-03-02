#pragma once
#include <cstddef>
#include <cstdint>
#include <shared_mutex>
#include <string>
#include <sys/types.h>
#include <vector>

#define HDD_MASK ((uint32_t)1 << 31)
#define HDD_BLOCK_IDX(__blk) (__blk | HDD_MASK)

class DiskManager {
public:
  static DiskManager &get_instance();

  void disk_open(std::string ssd_filename, std::string hdd_filename);
  void set_disk_block_size(uint32_t block_size);

  ssize_t metadata_read(void *buf, size_t nbyte, off_t offset);
  ssize_t metadata_write(const void *buf, size_t nbyte, off_t offset);

  ssize_t disk_read(void *buf, size_t nbyte, uint32_t pblock,
                    off_t pblock_offset);
  ssize_t disk_block_read(void *buf, uint32_t pblock);
  ssize_t disk_write(const void *buf, size_t nbyte, uint32_t pblock,
                     off_t pblock_offset);
  ssize_t disk_block_write(const void *buf, uint32_t pblock);

private:
  int ssd_fd_, hdd_fd_;
  uint32_t block_size_;

  mutable std::shared_mutex hdd_mutex_;
  mutable std::shared_mutex ssd_mutex_;

  DiskManager();

  ssize_t hdd_disk_read(void *buf, size_t nbyte, off_t offset);
  ssize_t hdd_disk_block_read(void *buf, uint64_t block_idx);
  ssize_t ssd_disk_read(void *buf, size_t nbyte, off_t offset);
  ssize_t ssd_disk_block_read(void *buf, uint64_t block_idx);

  ssize_t hdd_disk_write(const void *buf, size_t nbyte, off_t offset);
  ssize_t hdd_disk_block_write(const void *buf, uint64_t block_idx);
  ssize_t ssd_disk_write(const void *buf, size_t nbyte, off_t offset);
  ssize_t ssd_disk_block_write(const void *buf, uint64_t block_idx);
};