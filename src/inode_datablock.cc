#include "MetaData.h"
#include "common.h"
#include "disk.h"
#include "inode.h"
#include "types/ext4_inode.h"
#include <assert.h>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <glog/logging.h>
#include <vector>

#define IND_BLOCK_SIZE (block_size_ / sizeof(uint32_t))
#define DIND_BLOCK_SIZE (IND_BLOCK_SIZE * IND_BLOCK_SIZE)
#define TIND_BLOCK_SIZE (DIND_BLOCK_SIZE * IND_BLOCK_SIZE)
#define MAX_IND_BLOCK (EXT4_NDIR_BLOCKS + IND_BLOCK_SIZE)
#define MAX_DIND_BLOCK (MAX_IND_BLOCK + DIND_BLOCK_SIZE)
#define MAX_TIND_BLOCK (MAX_DIND_BLOCK + TIND_BLOCK_SIZE)

// not support extent right now
uint32_t InodeManager::get_data_pblock(const ext4_inode &inode,
                                       uint32_t lblock) {
  // uint32_t data_block_count = get_file_blocks_count(inode);
  // assert(lblock < data_block_count);

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

// return new lblock
void InodeManager::set_data_pblock(ext4_inode &inode, uint32_t lblock,
                                   uint32_t pblock) {
  if (lblock < EXT4_NDIR_BLOCKS) { // direct data block
    inode.i_block[lblock] = pblock;
  } else if (lblock < MAX_IND_BLOCK) {
    uint32_t index_pblock = inode.i_block[EXT4_IND_BLOCK];
    if (index_pblock == 0) {
      index_pblock = GET_INSTANCE(MetaDataManager).alloc_new_ssd_pblock();
      inode.i_block[EXT4_IND_BLOCK] = index_pblock;
    }

    set_data_lblock_ind(lblock - EXT4_NDIR_BLOCKS, index_pblock, pblock);
  } else if (lblock < MAX_DIND_BLOCK) {
    uint32_t dindex_pblock = inode.i_block[EXT4_DIND_BLOCK];
    if (dindex_pblock == 0) {
      dindex_pblock = GET_INSTANCE(MetaDataManager).alloc_new_ssd_pblock();
      inode.i_block[EXT4_DIND_BLOCK] = dindex_pblock;
    }

    set_data_lblock_dind(lblock - MAX_IND_BLOCK, dindex_pblock, pblock);
  } else if (lblock < MAX_TIND_BLOCK) {
    uint32_t tindex_pblock = inode.i_block[EXT4_TIND_BLOCK];
    if (tindex_pblock == 0) {
      tindex_pblock = GET_INSTANCE(MetaDataManager).alloc_new_ssd_pblock();
      inode.i_block[EXT4_TIND_BLOCK] = tindex_pblock;
    }

    set_data_lblock_tind(lblock - MAX_DIND_BLOCK, tindex_pblock, pblock);
  } else {
    LOG(FATAL) << "lblock exceed max data block size";
  }

  // Update inode
  uint64_t file_block_count = get_file_blocks_count(inode);
  if (lblock + 1 > file_block_count)
    set_file_blocks_count(inode, lblock + 1);
}

uint32_t InodeManager::get_data_pblock_ind(uint32_t lblock,
                                           uint32_t index_pblock) {
  assert(lblock < IND_BLOCK_SIZE);

  if (index_pblock == 0)
    return 0;

  uint32_t res;
  GET_INSTANCE(DiskManager)
      .disk_read(&res, sizeof(uint32_t), index_pblock,
                 lblock * sizeof(uint32_t));
  return res;
}

void InodeManager::set_data_lblock_ind(uint32_t lblock, uint32_t index_pblock,
                                       uint32_t pblock) {
  assert(lblock < IND_BLOCK_SIZE);

  GET_INSTANCE(DiskManager)
      .disk_write(&pblock, sizeof(uint32_t), index_pblock,
                  lblock * sizeof(uint32_t));
}

uint32_t InodeManager::get_data_pblock_dind(uint32_t lblock,
                                            uint32_t dindex_pblock) {
  assert(lblock < DIND_BLOCK_SIZE);

  if (dindex_pblock == 0)
    return 0;

  uint32_t index_pblock;
  uint32_t index_block_in_dind_offset =
      (lblock / MAX_IND_BLOCK) * sizeof(uint32_t);
  GET_INSTANCE(DiskManager)
      .disk_read(&index_pblock, sizeof(uint32_t), dindex_pblock,
                 index_block_in_dind_offset);

  lblock %= MAX_IND_BLOCK; // calculate index in index block
  return get_data_pblock_ind(lblock, index_pblock);
}

void InodeManager::set_data_lblock_dind(uint32_t lblock, uint32_t dindex_pblock,
                                        uint32_t pblock) {
  assert(lblock < DIND_BLOCK_SIZE);

  uint32_t index_pblock;
  uint32_t index_block_in_dind_offset =
      (lblock / MAX_IND_BLOCK) * sizeof(uint32_t);
  GET_INSTANCE(DiskManager)
      .disk_read(&index_pblock, sizeof(uint32_t), dindex_pblock,
                 index_block_in_dind_offset);

  // determine if need to create need index block
  if (index_pblock == 0) {
    index_pblock = GET_INSTANCE(MetaDataManager).alloc_new_ssd_pblock();
    GET_INSTANCE(DiskManager)
        .disk_write(&index_pblock, sizeof(uint32_t), dindex_pblock,
                    index_block_in_dind_offset);
  }

  lblock %= MAX_IND_BLOCK; // calculate index in index block
  set_data_lblock_ind(lblock, index_pblock, pblock);
}

uint32_t InodeManager::get_data_pblock_tind(uint32_t lblock,
                                            uint32_t tindex_pblock) {
  assert(lblock < TIND_BLOCK_SIZE);

  if (tindex_pblock == 0)
    return 0;

  uint32_t dindex_pblock;
  uint32_t dindex_block_in_tind_offset =
      (lblock / MAX_DIND_BLOCK) * sizeof(uint32_t);

  // Get dindex block from tind block
  GET_INSTANCE(DiskManager)
      .disk_read(&dindex_pblock, sizeof(uint32_t), tindex_pblock,
                 dindex_block_in_tind_offset);

  lblock %= MAX_DIND_BLOCK;
  return get_data_pblock_dind(lblock, dindex_pblock);
}

void InodeManager::set_data_lblock_tind(uint32_t lblock, uint32_t tindex_pblock,
                                        uint32_t pblock) {
  assert(lblock < TIND_BLOCK_SIZE);

  uint32_t dindex_pblock;
  uint32_t dindex_block_in_tind_offset =
      (lblock / MAX_DIND_BLOCK) * sizeof(uint32_t);
  GET_INSTANCE(DiskManager)
      .disk_read(&dindex_pblock, sizeof(uint32_t), tindex_pblock,
                 dindex_block_in_tind_offset);

  // determine if need to create need index block
  if (dindex_pblock == 0) {
    dindex_pblock = GET_INSTANCE(MetaDataManager).alloc_new_ssd_pblock();
    GET_INSTANCE(DiskManager)
        .disk_write(&dindex_pblock, sizeof(uint32_t), tindex_pblock,
                    dindex_block_in_tind_offset);
  }

  lblock %= MAX_DIND_BLOCK;
  set_data_lblock_dind(lblock, dindex_pblock, pblock);
}

void InodeManager::collect_file_pblock(ext4_inode &inode, std::vector<uint32_t> &pblock_vec) {
  for (uint32_t i = 0; i < EXT4_NDIR_BLOCKS; i++) {
    if (inode.i_block[i] != 0) {
      pblock_vec.push_back(inode.i_block[i]);
    }
  }

  if (inode.i_block[EXT4_IND_BLOCK] != 0) {
    collect_file_pblock_ind(inode.i_block[EXT4_IND_BLOCK], pblock_vec);
  }

  if (inode.i_block[EXT4_DIND_BLOCK] != 0) {
    collect_file_pblock_dind(inode.i_block[EXT4_DIND_BLOCK], pblock_vec);
  }

  if (inode.i_block[EXT4_TIND_BLOCK] != 0) {
    collect_file_pblock_tind(inode.i_block[EXT4_TIND_BLOCK], pblock_vec);
  }
}

void InodeManager::collect_file_pblock_ind(uint32_t index_block, std::vector<uint32_t> &pblock_vec) {
  uint32_t entry_num = block_size_ / sizeof(uint32_t);
  std::vector<uint32_t> index_table(entry_num, 0);
  GET_INSTANCE(DiskManager).disk_block_read(index_table.data(), index_block);
  
  // iter over index block
  for (uint32_t i = 0; i < entry_num; i++) {
    if (index_table[i] != 0) {
      pblock_vec.push_back(index_table[i]);
    }
  }

  pblock_vec.push_back(index_block);
}

void InodeManager::collect_file_pblock_dind(uint32_t dindex_block, std::vector<uint32_t> &pblock_vec) {
  uint32_t entry_num = block_size_ / sizeof(uint32_t);
  std::vector<uint32_t> dindex_table(entry_num, 0);
  GET_INSTANCE(DiskManager).disk_block_read(dindex_table.data(), dindex_block);
  
  // iter over index block
  for (uint32_t i = 0; i < entry_num; i++) {
    if (dindex_table[i] != 0) {
      collect_file_pblock_ind(dindex_table[i], pblock_vec);
    }
  }

  pblock_vec.push_back(dindex_block);
}

void InodeManager::collect_file_pblock_tind(uint32_t tindex_block, std::vector<uint32_t> &pblock_vec) {
  uint32_t entry_num = block_size_ / sizeof(uint32_t);
  std::vector<uint32_t> tindex_table(entry_num, 0);
  GET_INSTANCE(DiskManager).disk_block_read(tindex_table.data(), tindex_block);
  
  // iter over index block
  for (uint32_t i = 0; i < entry_num; i++) {
    if (tindex_table[i] != 0) {
      collect_file_pblock_dind(tindex_table[i], pblock_vec);
    }
  }

  pblock_vec.push_back(tindex_block);
}