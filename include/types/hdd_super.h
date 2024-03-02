#pragma once
#include <cstdint>
#include <fcntl.h>

struct hdd_group_desc {
  uint64_t bg_free_blocks_count;
  uint64_t bg_block_bitmap;
};

struct hdd_super_block {
  uint64_t s_file_size;
  uint64_t s_group_count;
};