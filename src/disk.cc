#include "disk.h"
#include "types/hdd_super.h"
#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <glog/logging.h>
#include <shared_mutex>
#include <sys/stat.h>
#include <sys/types.h>

#define BLOCKS2BYTES(__blks) ((uint64_t)(__blks)*block_size_)

static ssize_t pread_wrapper(int fd, void *buf, size_t nbytes, off_t offset);
static ssize_t pwrite_wrapper(int fd, const void *buf, size_t nbytes, off_t offset);

DiskManager &DiskManager::get_instance() {
  static DiskManager instance;
  return instance;
}

void DiskManager::disk_open(std::string ssd_filename,
                            std::string hdd_filename) {
  ssd_fd_ = open(ssd_filename.c_str(), O_RDWR);
  if (ssd_fd_ < 0) {
    LOG(FATAL) << "Open " << ssd_filename << " failed!";
  }

  hdd_fd_ = open(hdd_filename.c_str(), O_RDWR);
  if (hdd_fd_ < 0) {
    LOG(FATAL) << "Open " << hdd_filename << " failed!";
  }

  // check if hdd file initialize
  hdd_super_block hdd_super_;
  hdd_disk_read(&hdd_super_, sizeof(hdd_super_block), 0);
  if (hdd_super_.s_file_size == 0) {
    struct stat hdd_file_stat;
    int ret = fstat(hdd_fd_, &hdd_file_stat);
    if (ret == -1) {
      LOG(FATAL) << "Get " << hdd_filename << " stat failed!";
    }

    hdd_super_.s_file_size = hdd_file_stat.st_size;
    hdd_disk_write(&hdd_super_, sizeof(hdd_super_block), 0);
  }
}

ssize_t DiskManager::metadata_read(void *buf, size_t nbytes, off_t offset) {
  std::shared_lock lock(ssd_mutex_);
  return pread_wrapper(ssd_fd_, buf, nbytes, offset);
}

ssize_t DiskManager::metadata_write(const void *buf, size_t nbytes, off_t offset) {
  std::shared_lock lock(ssd_mutex_);
  return pwrite_wrapper(ssd_fd_, buf, nbytes, offset);
}

ssize_t DiskManager::disk_read(void *buf, size_t nbyte, uint32_t pblock, off_t pblock_offset) {
  if ((pblock & HDD_MASK) != 0) {
    pblock = pblock & (~HDD_MASK);
    off_t offset = pblock * block_size_ + pblock_offset;
    return hdd_disk_read(buf, nbyte, offset);
  } else {
    off_t offset = pblock * block_size_ + pblock_offset;
    return ssd_disk_read(buf, nbyte, offset);
  }
}

ssize_t DiskManager::disk_block_read(void *buf, uint32_t pblock) {
  if ((pblock & HDD_MASK) != 0) {
    pblock = pblock & (~HDD_MASK);
    return hdd_disk_block_read(buf, pblock);
  } else {
    return ssd_disk_block_read(buf, pblock);
  }
}

ssize_t DiskManager::disk_write(const void *buf, size_t nbyte, uint32_t pblock, off_t pblock_offset) {
  if ((pblock & HDD_MASK) != 0) {
    pblock = pblock & (~HDD_MASK);
    off_t offset = pblock * block_size_ + pblock_offset;
    return hdd_disk_write(buf, nbyte, offset);
  } else {
    off_t offset = pblock * block_size_ + pblock_offset;
    return ssd_disk_write(buf, nbyte, offset);
  }
}

ssize_t DiskManager::disk_block_write(const void *buf, uint32_t pblock) {
  if ((pblock & HDD_MASK) != 0) {
    pblock = pblock & (~HDD_MASK);
    return hdd_disk_block_write(buf, pblock);
  } else {
    return ssd_disk_block_write(buf, pblock);
  }
}

ssize_t DiskManager::hdd_disk_read(void *buf, size_t nbytes, off_t offset) {
  std::shared_lock lock(hdd_mutex_);
  return pread_wrapper(hdd_fd_, buf, nbytes, offset);
}

ssize_t DiskManager::ssd_disk_read(void *buf, size_t nbytes, off_t offset) {
  std::shared_lock lock(ssd_mutex_);
  return pread_wrapper(ssd_fd_, buf, nbytes, offset);
}

ssize_t DiskManager::hdd_disk_block_read(void *buf, uint64_t block_idx) {
  assert(block_size_ > 0);
  
  off_t offset = BLOCKS2BYTES(block_idx);
  return hdd_disk_read(buf, block_size_, offset);
}

ssize_t DiskManager::ssd_disk_block_read(void *buf, uint64_t block_idx) {
  assert(block_size_ > 0);
  
  off_t offset = BLOCKS2BYTES(block_idx);
  return ssd_disk_read(buf, block_size_, offset);
}

ssize_t DiskManager::hdd_disk_write(const void *buf, size_t nbytes, off_t offset) {
  std::shared_lock lock(hdd_mutex_);
  return pwrite_wrapper(hdd_fd_, buf, nbytes, offset);
}

ssize_t DiskManager::ssd_disk_write(const void *buf, size_t nbytes, off_t offset) {
  std::shared_lock lock(ssd_mutex_);
  return pwrite_wrapper(ssd_fd_, buf, nbytes, offset);
}

ssize_t DiskManager::hdd_disk_block_write(const void *buf, uint64_t block_idx) {
  assert(block_size_ > 0);

  std::shared_lock lock(hdd_mutex_);
  off_t offset = BLOCKS2BYTES(block_idx);
  return pwrite_wrapper(hdd_fd_, buf, block_size_, offset);
}

ssize_t DiskManager::ssd_disk_block_write(const void *buf, uint64_t block_idx) {
  assert(block_size_ > 0);

  std::shared_lock lock(ssd_mutex_);
  off_t offset = BLOCKS2BYTES(block_idx);
  return pwrite_wrapper(ssd_fd_, buf, block_size_, offset);
}

// ensure to read nbytes bytes
ssize_t pread_wrapper(int fd, void *buf, size_t nbytes, off_t offset) {
  assert(fd >= 0);
  ssize_t ret, len;

  void *cur_buf = buf;
  len = (ssize_t)nbytes;
  do {
    ret = pread(fd, cur_buf, len, offset);
    if (ret == -1) {
      if (errno == ENOENT)
        LOG(FATAL) << "File " << fd << " Not Found!";
      else
        LOG(FATAL) << "File " << fd << " exists IO Error! Errno: " << errno;
    }

    len -= ret;
    cur_buf = (std::byte *)cur_buf + ret;
  } while (len > 0 && ret > 0); // ensure to read all nbytes bytes
  return nbytes;
}

// ensure to write nbytes bytes
ssize_t pwrite_wrapper(int fd, const void *buf, size_t nbytes, off_t offset) {
  assert(fd >= 0);
  ssize_t ret, len;

  const void *cur_buf = buf;
  len = (ssize_t)nbytes;
  do {
    ret = pwrite(fd, cur_buf, len, offset);
    if (ret == -1) {
      if (errno == ENOENT)
        LOG(FATAL) << "File " << fd << " Not Found!";
      else
        LOG(FATAL) << "File " << fd << " exists IO Error! Errno: " << errno;
    }

    len -= ret;
    cur_buf = (std::byte *)cur_buf + ret;
  } while (len > 0 && ret > 0); // ensure to read all nbytes bytes
  return nbytes;
}

void DiskManager::set_disk_block_size(uint32_t block_size) {
  block_size_ = block_size;
}

DiskManager::DiskManager() : ssd_fd_(-1), hdd_fd_(-1), block_size_(0) {
}