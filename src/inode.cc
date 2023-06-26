#include "inode.h"
#include "MetaData.h"
#include "common.h"
#include "dcache.h"
#include "disk.h"
#include "types/ext4_dentry.h"
#include "types/ext4_inode.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <glog/logging.h>
#include <sys/types.h>

#define BLOCKS2BYTES(__blks) ((uint64_t)(__blks)*block_size_)
#define IND_BLOCK_SIZE (block_size_ / sizeof(uint32_t))
#define DIND_BLOCK_SIZE (IND_BLOCK_SIZE * IND_BLOCK_SIZE)
#define TIND_BLOCK_SIZE (DIND_BLOCK_SIZE * IND_BLOCK_SIZE)
#define MAX_IND_BLOCK (EXT4_NDIR_BLOCKS + IND_BLOCK_SIZE)
#define MAX_DIND_BLOCK (MAX_IND_BLOCK + DIND_BLOCK_SIZE)
#define MAX_TIND_BLOCK (MAX_DIND_BLOCK + TIND_BLOCK_SIZE)
#define ROOT_INODE 2

inline size_t get_path_len(const std::string &str, size_t npos) {
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
  return GET_INSTANCE(DCacheManager).init_root(ROOT_INODE);
}

int InodeManager::get_inode_by_path(const char *path, ext4_inode &inode) {
  uint32_t n = get_idx_by_path(path);
  if (n == 0)
    return  -ENOENT;
  return get_inode_by_idx(n, inode);
}

int InodeManager::get_inode_by_idx(uint32_t n, ext4_inode &res) {
  if (n == 0)
    return -ENOENT;
  n--; // Inode 0 doesn't exist on disk

  uint32_t idx_in_group = n % GET_INSTANCE(MetaDataManager).inodes_per_group();
  uint32_t inode_size = GET_INSTANCE(MetaDataManager).inode_size();
  size_t nbytes = std::min((size_t)inode_size, sizeof(ext4_inode));
  off_t off = GET_INSTANCE(MetaDataManager).inode_table_offset(n);
  off += idx_in_group * inode_size;

  GET_INSTANCE(DiskManager).ssd_disk_read(&res, nbytes, off);
  return 0;
}

ext4_dir_entry_2 *InodeManager::get_dentry(const ext4_inode &inode,
                                           off_t offset, DirCtx &ctx) {
  uint32_t lblock = offset / block_size_;
  uint32_t block_offset = offset % block_size_;
  uint64_t fize_size = get_file_size(inode);
  size_t un_offset = (size_t)offset;

  if (fize_size == un_offset) {
    return nullptr;
  }

  // check if current block in disk
  if (lblock != ctx.lblock) {
    uint64_t dir_data_pblock = get_data_pblock(inode, lblock);
    off_t dir_data_block_offset = BLOCKS2BYTES(dir_data_pblock);
    GET_INSTANCE(DiskManager)
        .ssd_disk_read(ctx.buf, block_size_, dir_data_block_offset);
    ctx.lblock = lblock;
  }

  return (ext4_dir_entry_2 *)&ctx.buf[block_offset];
}

uint32_t InodeManager::get_idx_by_path(const char *path_cstr) {
  assert(path_cstr[0] == '/'); // Paths from fuse are always absolute
  LOG(INFO) << "Look up: " << path_cstr;

  size_t npos = 0;
  std::string path = path_cstr;
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
    DCacheEntry *cache_ret = GET_INSTANCE(DCacheManager).lookup(cur_path, dc_entry);
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
      offset += dentry->rec_len;  // get next entry

      // Record each entry
      LOG(INFO) << "dentry: inode= " << dentry->inode << " rec_len=" << (uint32_t)dentry->rec_len << " name_len=" << (uint32_t)dentry->name_len << " file_type=" << (uint32_t)dentry->file_type << " name=" << dentry->name;

      // ignore invalid file
      if (dentry->inode == 0) continue;

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

// not support extent right now
uint32_t InodeManager::get_data_pblock(const ext4_inode &inode,
                                             uint32_t lblock) {
  uint32_t data_block_count =
      GET_INSTANCE(MetaDataManager).bytes_to_block(get_file_size(inode));
  assert(lblock <= data_block_count);

  if (lblock < EXT4_NDIR_BLOCKS) { // direct data block
    return inode.i_block[lblock];
  } else if (lblock < MAX_IND_BLOCK) {
    uint32_t index_block = inode.i_block[EXT4_IND_BLOCK];
    return get_data_pblock_ind(lblock - EXT4_NDIR_BLOCKS, index_block);
  } else if (lblock < MAX_DIND_BLOCK) {
    uint32_t dindex_block = inode.i_block[EXT4_DIND_BLOCK];
    return get_data_pblock_dind(lblock - MAX_IND_BLOCK, dindex_block);
  } else if (lblock < MAX_TIND_BLOCK) {
    uint32_t tindex_block = inode.i_block[EXT4_TIND_BLOCK];
    return get_data_pblock_tind(lblock - MAX_DIND_BLOCK, tindex_block);
  } else {
    LOG(FATAL) << "lblock exceed max data block size";
    return 0;
  }
}

uint32_t InodeManager::get_data_pblock_ind(uint32_t lblock,
                                           uint32_t index_block) {
  assert(lblock < IND_BLOCK_SIZE);

  uint32_t res;
  off_t index_block_offset =
      BLOCKS2BYTES(index_block) + lblock * sizeof(uint32_t);
  GET_INSTANCE(DiskManager)
      .ssd_disk_read(&res, sizeof(uint32_t), index_block_offset);
  return res;
}

uint32_t InodeManager::get_data_pblock_dind(uint32_t lblock,
                                            uint32_t dindex_block) {
  assert(lblock < DIND_BLOCK_SIZE);

  uint32_t index_block;
  uint32_t index_block_in_dind_offset =
      (lblock / MAX_IND_BLOCK) * sizeof(uint32_t);
  off_t index_block_entry_offset =
      BLOCKS2BYTES(dindex_block) + index_block_in_dind_offset;

  // Get index block from dind block
  GET_INSTANCE(DiskManager)
      .ssd_disk_read(&index_block, sizeof(uint32_t), index_block_entry_offset);

  lblock %= MAX_IND_BLOCK; // calculate index in index block
  return get_data_pblock_ind(lblock, index_block);
}

uint32_t InodeManager::get_data_pblock_tind(uint32_t lblock,
                                            uint32_t tindex_block) {
  assert(lblock < TIND_BLOCK_SIZE);

  uint32_t dindex_block;
  uint32_t dindex_block_in_tind_offset =
      (lblock / MAX_DIND_BLOCK) * sizeof(uint32_t);
  off_t dindex_block_entry_offset =
      BLOCKS2BYTES(tindex_block) + dindex_block_in_tind_offset;

  // Get dindex block from tind block
  GET_INSTANCE(DiskManager)
      .ssd_disk_read(&dindex_block, sizeof(uint32_t),
                     dindex_block_entry_offset);

  lblock %= MAX_DIND_BLOCK;
  return get_data_pblock_dind(lblock, dindex_block);
}