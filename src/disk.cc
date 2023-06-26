#include "disk.h"
#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <glog/logging.h>
#include <shared_mutex>
#include <sys/types.h>

static ssize_t pread_wrapper(int fd, void *buf, size_t nbytes, off_t offset);

DiskManager &DiskManager::get_instance() {
  static DiskManager instance;
  return instance;
}

void DiskManager::disk_open(std::string ssd_filename,
                            std::string hdd_filename) {
  hdd_fd_ = open(hdd_filename.c_str(), O_RDWR);
  if (hdd_fd_ < 0) {
    LOG(FATAL) << "Open " << hdd_filename << " failed!";
  }

  ssd_fd_ = open(ssd_filename.c_str(), O_RDWR);
  if (ssd_fd_ < 0) {
    LOG(FATAL) << "Open " << ssd_filename << " failed!";
  }
}

int DiskManager::hdd_disk_read(void *buf, size_t nbytes, off_t offset) {
  std::shared_lock lock(hdd_mutex_);
  ssize_t ret = pread_wrapper(hdd_fd_, buf, nbytes, offset);
  return ret;
}

int DiskManager::ssd_disk_read(void *buf, size_t nbytes, off_t offset) {
  std::shared_lock lock(ssd_mutex_);
  ssize_t ret = pread_wrapper(ssd_fd_, buf, nbytes, offset);
  return ret;
}

// ensure to read the whole block
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

DiskManager::DiskManager() {
  ssd_fd_ = -1, hdd_fd_ = -1;
}