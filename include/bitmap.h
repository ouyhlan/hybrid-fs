#pragma once

#include "common.h"
#include "disk.h"
#include <cassert>
#include <cstdint>
#include <vector>
class Bitmap {
public:
  Bitmap(uint32_t block_size) {
    assert(block_size % sizeof(uint32_t) == 0);
    buf_ = std::vector<uint32_t>(block_size / sizeof(uint32_t), 0);
    size_ = block_size * sizeof(uint32_t) * 8;
  }

  void load(uint32_t bitmap_pblock) {
    GET_INSTANCE(DiskManager).disk_block_read(buf_.data(), bitmap_pblock);
  }

  void save(uint32_t bitmap_pblock) {
    GET_INSTANCE(DiskManager).disk_block_write(buf_.data(), bitmap_pblock);
  }

  bool lookup(uint32_t bitmap_idx) {
    assert(bitmap_idx < size_);

    uint32_t index = bitmap_idx / (sizeof(uint32_t) * 8);
    uint32_t bit_index = bitmap_idx % (sizeof(uint32_t) * 8);
    return buf_[index] & (1 << bit_index);
  }

  void set(uint32_t bitmap_idx) {
    assert(bitmap_idx < size_);

    uint32_t index = bitmap_idx / (sizeof(uint32_t) * 8);
    uint32_t bit_index = bitmap_idx % (sizeof(uint32_t) * 8);
    buf_[index] |= 1 << bit_index;
  }

  void unset(uint32_t bitmap_idx) {
    assert(bitmap_idx < size_);

    uint32_t index = bitmap_idx / (sizeof(uint32_t) * 8);
    uint32_t bit_index = bitmap_idx % (sizeof(uint32_t) * 8);
    buf_[index] &= ~(1 << bit_index);
  }

  uint32_t size() {
    return size_;
  }

private:
  std::vector<uint32_t> buf_;
  uint32_t size_;
};