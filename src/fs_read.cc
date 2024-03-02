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
  
  if (size == 0 || start_offset == 0) 
    return 0;

  size_t first_read_size = size;
  if (start_lblock != end_lblock) { // cross block reading
    first_read_size = ALIGN_TO(offset, block_size) - offset;
  }

  uint64_t start_pblock = GET_INSTANCE(InodeManager).get_data_pblock(inode, start_lblock);
  if (start_pblock == 0) // sparse file
    return first_read_size;

  GET_INSTANCE(DiskManager).disk_read(buf, first_read_size, start_pblock, start_offset);
  LOG(INFO) << "Read " << first_read_size << "bytes from block #" << start_pblock;
  return first_read_size;
}

int fs_read(const char *path, char *buf, size_t size, off_t offset,
         fuse_file_info *fi) {
  assert(offset >= 0);
  LOG(INFO) << "Read begin:";
  LOG(INFO) << "read(" << path << ", buf, " << size << ", " << offset
             << ", fi->fh=" << fi->fh << ")";

  if (((fi->flags & O_ACCMODE) == O_WRONLY))
      return -EACCES;

  size_t bytes;
  size_t ret = 0;
  size_t un_offset = (size_t)offset;
  uint32_t block_size = GET_INSTANCE(MetaDataManager).block_size();
  ext4_inode inode;

  int get_inode_ret =
      GET_INSTANCE(InodeManager).get_inode_by_idx(fi->fh, inode);
  if (get_inode_ret < 0) {
    return get_inode_ret;
  }
  
  // truncate size
  size = truncate_size(inode, size, offset);

  // read the first block and doing the alignment
  bytes = first_read(inode, buf, size, offset);

  ret = bytes;
  buf += bytes;
  un_offset += bytes;
  for (uint32_t lblock = un_offset / block_size; size > ret; lblock++) {
    bytes = (size - ret) > block_size ? block_size : size - ret;

    uint64_t pblock = GET_INSTANCE(InodeManager).get_data_pblock(inode, lblock);
    if (pblock) { 
      GET_INSTANCE(DiskManager).disk_read(buf, bytes, pblock, 0);
      LOG(INFO) << "Read " << bytes << " from block #" << pblock;
    } else {  // deal with sparse file
      memset(buf, 0, bytes);
      LOG(INFO) << "Sparse file, skipping " << bytes << " bytes";
    }
    ret += bytes;
    buf += bytes;
  }
  assert(size == ret);
  LOG(INFO) << "Read done";
  return ret;
}