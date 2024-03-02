#include "ops.h"
#include "MetaData.h"
#include "common.h"
#include "inode.h"
#include "types/ext4_dentry.h"
#include "types/ext4_inode.h"
#include <cstddef>
#include <fcntl.h>
#include <glog/logging.h>

int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
            fuse_file_info *fi, fuse_readdir_flags flags) {
  LOG(INFO) << "Readdir begin:";
  (void)fi;
  (void)offset;
  (void)flags;

  struct stat st;
  ext4_inode inode;
  fuse_fill_dir_flags fill_flags = FUSE_FILL_DIR_PLUS;  // fix Input/output error

  uint32_t block_size = GET_INSTANCE(MetaDataManager).block_size();
  DirCtx dir_ctx(block_size);
  
  int ret = GET_INSTANCE(InodeManager).get_inode_by_path(path, inode);
  if (ret < 0)
    return ret;

  off_t dentry_off = 0;
  ext4_dir_entry_2 *dentry = nullptr;
  while ((dentry =
              GET_INSTANCE(InodeManager).get_dentry(inode, dentry_off, dir_ctx)) !=
         nullptr) {
    dentry_off += dentry->rec_len;

    if (dentry->inode == 0)
      continue;

    // since cfg->use_ino is not set, no need to set this
    // st.st_ino = dentry->inode;
    st.st_mode = inode.i_mode;
    std::string filename(dentry->name, (size_t)dentry->name_len);

    LOG(INFO) << st.st_ino << " " << st.st_mode << " " << filename;
    if (filler(buf, filename.c_str(), &st, 0, fill_flags))
      break;
  }

  LOG(INFO) << "Readdir done!";
  return 0;
}