# HDD 和 SSD 混合文件系统

## 概述
本项目我实现的是一个基于 fuse 的 HDD 和 SSD 混合文件系统，总体的框架基于 ext2 文件系统，并在此基础上增加了对用户透明的 HDD 与 SSD 混合存储架构。

以下是本文件系统支持的操作：
```c++
static struct fuse_operations fs_ops = {
  .getattr = fs_getattr,
  .mknod = fs_mknod,
  .mkdir = fs_mkdir,
  .unlink = fs_unlink,
  .rmdir = fs_rmdir,
  .open = fs_open,
  .read = fs_read,
  .write = fs_write,
  .readdir = fs_readdir,
  .init = fs_init,
}
```

根据这些操作函数的要求，我实现了以下模块：
- 磁盘管理
- 文件系统元数据管理
- Inode 管理
- 缓存管理

在具体代码的实现中，我参考了 ext4fuse 的部分代码，这是⼀个利⽤ C 语⾔实现的基于 fuse 框架的 ext4 ⽂件系统。然⽽，它的代码只实现了⼀个基于单磁盘的只读⽂件系统。本⽂件系统利⽤ C++ 语⾔实现了⼀个⽀持⽂件增删查改的混合磁盘 ext2 ⽂件系统。