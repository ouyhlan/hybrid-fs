#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <sys/types.h>
#include <unordered_map>

struct DCacheEntry {
  const DCacheEntry *parent;
  uint32_t inode_idx;
  DCacheEntry(const DCacheEntry *parent, uint32_t inode)
      : parent(parent), inode_idx(inode) {}
};

class DCacheManager {
public:
  static DCacheManager &get_instance();
  int init_root(uint32_t root_inode_idx);
  DCacheEntry *get_root();
  DCacheEntry *lookup(const std::string &filename, const DCacheEntry* parent);

  void insert(const std::string &filename, uint32_t inode_idx, const DCacheEntry* parent);
  void remove(const std::string &filename, uint32_t parent_inode_idx);

private:
  struct DCacheEntry *root_;
  std::unordered_map<std::string, struct DCacheEntry *> hash_table_;

  DCacheManager();
  ~DCacheManager();
};