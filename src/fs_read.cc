#include "disk.h"
#include "ops.h"
#include "common.h"
#include "inode.h"
#include "MetaData.h"
#include "types/ext4_inode.h"
#include <cassert>
#include <cstddef>
#include <cstring>
#include <fcntl.h>
#include <glog/logging.h>

// truncate the read size if exceeds file size
static size_t truncate_size(const ext4_inode &inode, size_t size, size_t offset) {
  uint64_t file_size = GET_INSTANCE(InodeManager).get_file_size(inode);
  
  if (offset >= file_size) {
    return 0;
  }

  if ((offset + size) >= file_size) {
    LOG(INFO) << "Truncatie " << offset + size << " to " << file_size;
    return file_size - offset;
  }

  return size;
}

static size_t first_read(const ext4_inode &inode, char *buf, size_t size, off_t offset) {
  // offset = 0 size = BLOCK_SIZE on the same block
  uint32_t block_size = GET_INSTANCE(MetaDataManager).block_size();
  uint32_t start_lblock = offset / block_size;
  uint32_t end_lblock = (offset + size - 1) / block_size;
  uint32_t start_offset = offset % block_size;
  
  if (size ==0 || start_offset == 0) return 0;

  uint64_t start_pblock = GET_INSTANCE(InodeManager).get_data_pblock(inode, start_pblock);
  size_t first_read_size = size;

  if (start_lblock != end_lblock) { // cross block reading
    first_read_size = ALIGN_TO(offset, block_size) - offset;
  }

  off_t start_pblock_offset = start_pblock * block_size + start_offset;
  GET_INSTANCE(DiskManager).ssd_disk_read(buf, first_read_size, start_pblock_offset);
  return first_read_size;
}

int fs_read(const char *path, char *buf, size_t size, off_t offset,
         fuse_file_info *fi) {
  assert(offset >= 0);
  LOG(INFO) << "Read begin:";
  LOG(INFO) << "read(" << path << ", buf, " << size << ", " << offset
             << ", fi->fh=" << fi->fh << ")";

  size_t ret = 0;
  size_t un_offset = (size_t)offset;
  ext4_inode inode;

  int get_inode_ret =
      GET_INSTANCE(InodeManager).get_inode_by_idx(fi->fh, inode);
  if (get_inode_ret < 0) {
    return get_inode_ret;
  }
  
  // truncate size
  size = truncate_size(inode, size, offset);

  // read the first block and doing the alignment
  ret = first_read(inode, buf, size, offset);

  buf += ret;
  un_offset += ret;
  size_t bytes;
  uint32_t block_size = GET_INSTANCE(MetaDataManager).block_size();
  for (uint32_t lblock = un_offset / block_size; size > ret; lblock++) {
    uint64_t pblock = GET_INSTANCE(InodeManager).get_data_pblock(inode, lblock);

    if (pblock) {
      bytes = (size - ret) > block_size ? block_size : size - ret;
      off_t pblock_offset = pblock * block_size;
      GET_INSTANCE(DiskManager).ssd_disk_read(buf, bytes, pblock_offset);
    } else {
      bytes = size - ret;
      if (bytes > block_size) {
        bytes = block_size;
      }
      memset(buf, 0, bytes);
      LOG(INFO) << "Sparse file, skipping " << bytes << " bytes";
    }
    ret += bytes;
    buf += bytes;
  }
  assert(size == ret);
  return ret;
}