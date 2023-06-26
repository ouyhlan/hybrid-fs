#pragma once
#include <cstddef>
#include <shared_mutex>
#include <string>
#include <vector>

class DiskManager {
public:
  static DiskManager &get_instance();

  void disk_open(std::string ssd_filename, std::string hdd_filename);

  int hdd_disk_read(void *buf, size_t nbyte, off_t offset);
  int ssd_disk_read(void *buf, size_t nbyte, off_t offset);

private:
  int ssd_fd_, hdd_fd_;
  mutable std::shared_mutex hdd_mutex_;
  mutable std::shared_mutex ssd_mutex_;
  DiskManager();
};