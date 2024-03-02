#include "ops.h"
#include "MetaData.h"
#include "common.h"
#include "disk.h"
#include "inode.h"
#include "types/ext4_inode.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <glog/logging.h>

static size_t first_write(ext4_inode &inode, const char *buf, size_t size, off_t offset) {
  uint32_t block_size = GET_INSTANCE(MetaDataManager).block_size();
  uint32_t start_lblock = offset / block_size;
  uint32_t end_lblock = (offset + size - 1) / block_size;
  uint32_t start_offset = offset % block_size;
  
  if (size == 0 || start_offset == 0) 
    return 0;

  uint64_t start_pblock = GET_INSTANCE(InodeManager).get_data_pblock(inode, start_lblock);
  if (start_pblock == 0) {  // fill in empty block
    start_pblock = GET_INSTANCE(MetaDataManager).alloc_new_pblock(start_lblock);
    GET_INSTANCE(InodeManager).set_data_pblock(inode, start_lblock, start_pblock);
  }
  
  size_t first_write_size = size;
  if (start_lblock != end_lblock) { // cross block reading
    first_write_size = ALIGN_TO(offset, block_size) - offset;
  }

  GET_INSTANCE(DiskManager).disk_write(buf, first_write_size, start_pblock, start_offset);
  // DLOG(INFO) << "Write " << first_write_size << " to block #" << start_pblock;
  return first_write_size;
}

int fs_write(const char *path, const char *buf, size_t size, off_t offset,
             struct fuse_file_info *fi) {
  assert(offset >= 0);
  LOG(INFO) << "Write begin: ";
  LOG(INFO) << "write( " << path << ", buf, " << size << ", " << offset
            << ", fi->fh=" << fi->fh << ")";
  GET_INSTANCE(MetaDataManager).log_hdd_stat();

  uint32_t inode_idx;
  ext4_inode inode;
  if (fi) {
    if (((fi->flags & O_ACCMODE) == O_RDONLY))
      return -EACCES;
    inode_idx = fi->fh;
  } else {
    inode_idx= GET_INSTANCE(InodeManager).get_idx_by_path(path);
  }

  int get_inode_ret = GET_INSTANCE(InodeManager).get_inode_by_idx(inode_idx, inode);
  if (get_inode_ret < 0) {
    return get_inode_ret;
  }

  size_t bytes;
  size_t ret = 0;
  size_t un_offset = (size_t)offset;
  uint32_t block_size = GET_INSTANCE(MetaDataManager).block_size();

  // write the first block and doing the alignment
  bytes = first_write(inode, buf, size, offset);

  ret = bytes;
  buf += bytes;
  un_offset += bytes;
  for (uint32_t lblock = un_offset / block_size; size > ret; lblock++) {
    uint64_t pblock = GET_INSTANCE(InodeManager).get_data_pblock(inode, lblock);
    if (pblock == 0) {  // fill in empty block
      pblock = GET_INSTANCE(MetaDataManager).alloc_new_pblock(lblock);
      GET_INSTANCE(InodeManager).set_data_pblock(inode, lblock, pblock);
    }

    bytes = (size - ret) > block_size ? block_size : size - ret;
    GET_INSTANCE(DiskManager).disk_write(buf, bytes, pblock, 0);
    // DLOG(INFO) << "Write " << bytes << " to block #" << pblock;

    ret += bytes;
    buf += bytes;
  }

  assert(size == ret);

  uint64_t file_size = GET_INSTANCE(InodeManager).get_file_size(inode);
  if ((uint64_t)offset + size > file_size) {
    GET_INSTANCE(InodeManager).set_file_size(inode, (size_t)offset + size);
  }
  GET_INSTANCE(InodeManager).update_disk_inode(inode_idx, inode);

  GET_INSTANCE(MetaDataManager).log_hdd_stat();
  LOG(INFO) << "Write done";
  return ret;
}