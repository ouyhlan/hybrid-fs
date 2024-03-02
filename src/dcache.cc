#include "dcache.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <glog/logging.h>
#include <string>

DCacheManager &DCacheManager::get_instance() {
  static DCacheManager instance;
  return instance;
}

int DCacheManager::init_root(uint32_t root_inode_idx) {
  if (root_ != nullptr) {
    LOG(WARNING) << "Reinitializing dcache root. Ignore it.";
    return -1;
  }

  root_ = new DCacheEntry(root_, root_inode_idx);
  std::string key = "$" + std::to_string(root_inode_idx) + "/";
  hash_table_[key] = root_;
  return 0;
}

DCacheEntry *DCacheManager::get_root() { return root_; }

void DCacheManager::insert(const std::string &filename, uint32_t inode_idx, const DCacheEntry* parent) {
  std::string key = "$" + std::to_string(parent->inode_idx) + filename;
  if (hash_table_.count(key) > 0 && hash_table_[key] != nullptr)
    delete hash_table_[key];
  hash_table_[key] = new DCacheEntry(parent, inode_idx);
}

// if not found return nullptr
DCacheEntry *DCacheManager::lookup(const std::string &filename,
                                   const DCacheEntry *parent) {
  std::string key = "$" + std::to_string(parent->inode_idx) + filename;
  if (hash_table_.count(key) > 0 && hash_table_[key] != nullptr)
    return hash_table_[key];
  else
    return nullptr;
}

void DCacheManager::remove(const std::string &filename, uint32_t parent_inode_idx) {
  std::string key = "$" + std::to_string(parent_inode_idx) + filename;
  if (hash_table_.count(key) > 0) {
    delete hash_table_[key];
    hash_table_[key] = nullptr;
  }
}

DCacheManager::DCacheManager() : root_(nullptr) {}

DCacheManager::~DCacheManager() {
  for (auto &c : hash_table_) {
    delete c.second;
  }
}