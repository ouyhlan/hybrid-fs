#include "inode.h"
#include "MetaData.h"
#include "common.h"
#include "dcache.h"
#include "disk.h"
#include "types/ext4_dentry.h"
#include "types/ext4_inode.h"
#include <algorithm>
#include <bits/types/time_t.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <glog/logging.h>
#include <string>
#include <sys/types.h>
#include <vector>

#define ROOT_INODE 2

inline static uint16_t cal_min_rec_len(const ext4_dir_entry_2 &dentry) {
  uint16_t res = sizeof(uint32_t) + sizeof(uint16_t) + sizeof(uint8_t) * 2 +
                 dentry.name_len;
  return ALIGN_TO(res, sizeof(uint32_t));
}

// copy content
inline static void copy_dentry(const ext4_dir_entry_2 &from,
                               ext4_dir_entry_2 *to) {
  to->inode = from.inode;
  to->rec_len = from.rec_len;
  to->name_len = from.name_len;
  to->file_type = from.file_type;

  for (uint8_t i = 0; i < from.name_len; i++) {
    to->name[i] = from.name[i];
  }
}

inline static size_t get_path_len(const std::string &str, size_t npos) {
  size_t cur_pos = npos;
  while (cur_pos < str.size() && str[cur_pos] != '/')
    cur_pos++;
  return cur_pos - npos;
}

InodeManager &InodeManager::get_instance() {
  static InodeManager instance;
  return instance;
}

// Called after Super Block initialized
int InodeManager::init() {
  block_size_ = GET_INSTANCE(MetaDataManager).block_size();
  inode_size_ = std::min((size_t)GET_INSTANCE(MetaDataManager).inode_size(),
                         sizeof(ext4_inode));
  return GET_INSTANCE(DCacheManager).init_root(ROOT_INODE);
}

int InodeManager::get_inode_by_path(const std::string &path,
                                    ext4_inode &inode) {
  uint32_t n = get_idx_by_path(path);
  if (n == 0)
    return -ENOENT;
  return get_inode_by_idx(n, inode);
}

int InodeManager::get_inode_by_idx(uint32_t n, ext4_inode &res) {
  if (n == 0)
    return -ENOENT;

  off_t off = GET_INSTANCE(MetaDataManager).inode_table_entry_offset(n);
  GET_INSTANCE(DiskManager).metadata_read(&res, inode_size_, off);
  LOG(INFO) << "Read Inode #" << n << " from offset: " << off;
  return 0;
}

void InodeManager::update_disk_inode(uint32_t inode_idx, const ext4_inode &inode) {
  assert(inode_idx > 0);

  off_t offset =
      GET_INSTANCE(MetaDataManager).inode_table_entry_offset(inode_idx);
  GET_INSTANCE(DiskManager).metadata_write(&inode, inode_size_, offset);
  LOG(INFO) << "Write Inode #" << inode_idx << " from offset: " << offset << " : " << inode_str(inode);
}

ext4_dir_entry_2 *InodeManager::get_dentry(const ext4_inode &inode,
                                           off_t offset, DirCtx &ctx) {
  uint32_t lblock = offset / block_size_;
  uint32_t block_offset = offset % block_size_;
  uint32_t file_block_count = get_file_blocks_count(inode);
  size_t un_offset = (size_t)offset;

  if (file_block_count == lblock) {
    return nullptr;
  }

  // check if current block in disk
  if (lblock != ctx.lblock) {
    uint32_t dir_data_pblock = get_data_pblock(inode, lblock);
    GET_INSTANCE(DiskManager).disk_block_read(ctx.buf, dir_data_pblock);
    ctx.lblock = lblock;
  }

  return (ext4_dir_entry_2 *)&ctx.buf[block_offset];
}

uint32_t InodeManager::get_idx_by_path(const std::string &path) {
  assert(path[0] == '/'); // Paths from fuse are always absolute
  LOG(INFO) << "Look up: " << path;

  size_t npos = 0;
  const DCacheEntry *dc_entry = GET_INSTANCE(DCacheManager).get_root();
  DirCtx dir_ctx(block_size_);
  ext4_inode prefix_inode;

  // set the dc_entry to "path"
  do {
    // Ignore the prefix '/'
    while (npos < path.size() && path[npos] == '/')
      npos++;

    // update npos new position
    size_t path_len = get_path_len(path, npos);
    std::string cur_path = path.substr(npos, path_len);
    npos += path_len;

    // check if reach the end of path
    if (path_len == 0) {
      break;
    }

    // deal with "." and ".."
    if ((path_len <= 2) && (path[npos] == '.') &&
        ((path_len == 1) || (path[npos + 1] == '.'))) {
      if (path_len == 2) { // ".."
        dc_entry = dc_entry->parent;
      }
      continue;
    }

    // checkout cache
    DCacheEntry *cache_ret =
        GET_INSTANCE(DCacheManager).lookup(cur_path, dc_entry);
    if (cache_ret != nullptr) { // find cache
      LOG(INFO) << "Find directory: " << cur_path << " in cache";
      dc_entry = cache_ret;
      continue;
    }

    // get prefix inode
    get_inode_by_idx(dc_entry->inode_idx, prefix_inode);

    // current path is prefix/path.substr(npos)
    // check if prefix is a valid directory
    if (!S_ISDIR(prefix_inode.i_mode)) { // prefix is not a directory
      // since path_len != 0 then this is not a valid path
      LOG(INFO) << "Directory: " << cur_path << "'s prefix is not a directory";
      dc_entry = nullptr;
      break;
    }

    // Loading directory file
    off_t offset = 0;
    ext4_dir_entry_2 *dentry = nullptr;
    LOG(INFO) << "Inode #" << dc_entry->inode_idx << " 's content:";
    while ((dentry = get_dentry(prefix_inode, offset, dir_ctx)) != nullptr) {
      offset += dentry->rec_len; // get next entry

      // Record each entry
      LOG(INFO) << dentry_str(*dentry);

      // ignore invalid file
      if (dentry->inode == 0)
        continue;

      std::string cur_filename(dentry->name, (size_t)dentry->name_len);

      // deal with "." and ".."
      if (cur_filename == "." || cur_filename == "..") {
        continue;
      }

      // cache all the iter entry
      GET_INSTANCE(DCacheManager).insert(cur_filename, dentry->inode, dc_entry);
    }

    // checkout cache again
    cache_ret = GET_INSTANCE(DCacheManager).lookup(cur_path, dc_entry);
    if (cache_ret == nullptr) { // cannot find cache
      LOG(INFO) << "Cannot find " << cur_path << " in path \"" << path << "\"";
      dc_entry = nullptr;
      break;
    }
    dc_entry = cache_ret;

  } while (npos < path.size());

  return (dc_entry == nullptr ? 0 : dc_entry->inode_idx);
}

uint64_t InodeManager::get_file_size(const ext4_inode &inode) {
  return ((uint64_t)inode.i_size_high << 32) | inode.i_size_lo;
}

void InodeManager::set_file_size(ext4_inode &inode, uint64_t new_size) {
  inode.i_size_lo = new_size & (~(uint32_t)0);
  inode.i_size_high = (new_size >> 32) & (~(uint32_t)0);
}

uint32_t InodeManager::get_file_blocks_count(const ext4_inode &inode) {
  return inode.i_blocks_lo / (block_size_ / 512);
}

void InodeManager::set_file_blocks_count(ext4_inode &inode,
                                         uint32_t new_blocks_count) {
  inode.i_blocks_lo = new_blocks_count * (block_size_ / 512);
}

// Add entry to directory and update disk content
void InodeManager::add_dentry(ext4_inode &prefix_inode,
                              const ext4_dir_entry_2 &new_dentry) {
  uint16_t new_min_rec_len = cal_min_rec_len(new_dentry);

  off_t offset = 0;
  uint16_t iter_min_rec_len;
  ext4_dir_entry_2 *iter_dentry = nullptr;
  DirCtx dir_ctx(block_size_);
  while ((iter_dentry = get_dentry(prefix_inode, offset, dir_ctx)) != nullptr) {
    // The case which first entry in this block is deleted
    if (offset == 0 && iter_dentry->inode == 0 ) {
      if (iter_dentry->rec_len >= new_min_rec_len) {
        break;
      } else {
        continue;
      }
    }
    
    iter_min_rec_len = cal_min_rec_len(*iter_dentry);

    // check if there is enough space to add this entry
    if (new_min_rec_len + iter_min_rec_len <= iter_dentry->rec_len) {
      break;
    }

    offset += iter_dentry->rec_len; // get next entry
  }

  // revise new block
  // uint32_t lblock = offset / block_size_;
  uint32_t block_offset = offset % block_size_;
  ext4_dir_entry_2 *new_add_entry = nullptr;
  if (iter_dentry != nullptr) {
    if (iter_dentry->inode != 0) {
      new_add_entry =
          (ext4_dir_entry_2 *)&dir_ctx.buf[block_offset + iter_min_rec_len];
      copy_dentry(new_dentry, new_add_entry);

      // update rec_len
      LOG(INFO) << "iter_dentry->rec_len=" << iter_dentry->rec_len << " iter_min_rec_len=" << iter_min_rec_len;
      new_add_entry->rec_len = iter_dentry->rec_len - iter_min_rec_len;
      iter_dentry->rec_len = iter_min_rec_len;
      LOG(INFO) << "Add " << dentry_str(*new_add_entry) << " under "
                << dentry_str(*iter_dentry);
    } else {  // dentry in the first
      uint16_t rec_len = iter_dentry->rec_len;
      copy_dentry(new_dentry, iter_dentry);
      iter_dentry->rec_len = rec_len;

      LOG(INFO) << "Add" << dentry_str(*iter_dentry);
    }

  } else {
    uint64_t file_size = get_file_size(prefix_inode);
    uint64_t block_count = get_file_blocks_count(prefix_inode);

    // get new data block
    uint32_t pblock = GET_INSTANCE(MetaDataManager).alloc_new_ssd_pblock();

    // set new dir data block
    assert(((uint32_t)-1 + 1) == 0);
    dir_ctx.lblock += 1;
    set_data_pblock(prefix_inode, dir_ctx.lblock, pblock);
    set_file_size(prefix_inode, file_size + block_size_);

    // set new data block content
    memset(dir_ctx.buf, 0, block_size_);
    new_add_entry = (ext4_dir_entry_2 *)dir_ctx.buf;
    copy_dentry(new_dentry, new_add_entry);

    // update rec_len
    new_add_entry->rec_len = block_size_;
    LOG(INFO) << "Add " << dentry_str(*new_add_entry) << " in new data block";
    LOG(INFO) << "Data: {lblock: " << dir_ctx.lblock << " pblock: " << pblock << "}";
    LOG(INFO) << "File block count: " << get_file_blocks_count(prefix_inode);
  }

  // update directory content in disk
  uint32_t dir_data_pblock = get_data_pblock(prefix_inode, dir_ctx.lblock);
  GET_INSTANCE(DiskManager).disk_block_write(dir_ctx.buf, dir_data_pblock);
}

// remove dentry from directory
void InodeManager::rm_dentry(ext4_inode &prefix_inode, uint32_t cur_inode_idx) {
  ext4_dir_entry_2 *iter_dentry, *pre_dentry;
  DirCtx dir_ctx(block_size_);
  
  // Directory must have . ..
  pre_dentry = get_dentry(prefix_inode, 0, dir_ctx);
  uint32_t prefix_inode_idx = pre_dentry->inode;
  
  std::string filename;
  off_t offset = pre_dentry->rec_len;
  while ((iter_dentry = get_dentry(prefix_inode, offset, dir_ctx)) != nullptr) {
    // check if found that entry
    if (iter_dentry->inode == cur_inode_idx) {
      filename = {iter_dentry->name, (size_t)iter_dentry->name_len};
      break;
    } else {
      pre_dentry = iter_dentry;
    }

    offset += iter_dentry->rec_len; // get next entry
  }

  // check if cur_inode_idx in a single block
  if (iter_dentry->rec_len == block_size_) {
    iter_dentry->inode = 0;
  } else {
    pre_dentry->rec_len += iter_dentry->rec_len;
    iter_dentry->inode = 0;
  }

  // remove dcache
  GET_INSTANCE(DCacheManager).remove(filename, prefix_inode_idx);

  // update directory content in disk
  uint32_t dir_data_pblock = get_data_pblock(prefix_inode, dir_ctx.lblock);
  GET_INSTANCE(DiskManager).disk_block_write(dir_ctx.buf, dir_data_pblock);
}

void InodeManager::rm_dir(ext4_inode &cur_inode, uint32_t cur_inode_idx) {
  off_t dentry_off = 0;
  uint32_t block_size = GET_INSTANCE(MetaDataManager).block_size();
  DirCtx dir_ctx(block_size);

  // ignore . ..
  ext4_dir_entry_2 *dentry = get_dentry(cur_inode, dentry_off, dir_ctx);
  dentry_off += dentry->rec_len;
  dentry = GET_INSTANCE(InodeManager).get_dentry(cur_inode, dentry_off, dir_ctx);
  dentry_off += dentry->rec_len;

  while ((dentry =
              GET_INSTANCE(InodeManager).get_dentry(cur_inode, dentry_off, dir_ctx)) !=
         nullptr) {
    dentry_off += dentry->rec_len;

    if (dentry->inode == 0)
      continue;

    ext4_inode iter_inode;
    get_inode_by_idx(dentry->inode, iter_inode);

    if ((dentry->file_type & 0x2) != 0) {
      rm_dir(iter_inode, dentry->inode);
    } else {
      rm_file(iter_inode, dentry->inode);
    }
  }
}

void InodeManager::rm_file(ext4_inode &cur_inode, uint32_t cur_inode_idx) {
  std::vector<uint32_t> pblock_to_remove;
  
  // rm_dentry(prefix_inode, cur_inode_idx);
  collect_file_pblock(cur_inode, pblock_to_remove);
  GET_INSTANCE(MetaDataManager).free_pblock(pblock_to_remove);
  GET_INSTANCE(MetaDataManager).free_inode(cur_inode_idx);
}