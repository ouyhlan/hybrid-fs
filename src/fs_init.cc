#include "ops.h"
#include "MetaData.h"
#include "common.h"
#include "inode.h"
#include <glog/logging.h>

void *fs_init(fuse_conn_info *conn, fuse_config *cfg) {
  (void)cfg;

  LOG(INFO) << "Init begin:";
  LOG(INFO) << "Using FUSE protocol " << conn->proto_major << "."
            << conn->proto_minor;

  // fill in super block
  GET_INSTANCE(MetaDataManager).super_block_fill();

  // fill in gdt
  GET_INSTANCE(MetaDataManager).gdt_fill();

  // Initialize root inode
  GET_INSTANCE(InodeManager).init();

   LOG(INFO) << "Init done!";
  return NULL;
}