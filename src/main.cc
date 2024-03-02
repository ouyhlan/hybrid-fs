#include "ops.h"
#include "common.h"
#include "cxxopts.hpp"
#include "disk.h"
#include <err.h>
#include <glog/logging.h>
#include <iostream>
#include <string>

struct Fs {
  std::string hdd_path;
  std::string ssd_path;
} fs;

static void print_usage(char *prog_name) {
  std::cout << "Usage: " << prog_name
            << " [fuse_options] <mountpoint> [fs_options]\n\n";
  //   << "File-system specific options:\n"
  //      "    --hdd_filename=<s>      Name of the Filesystem hdd file\n"
  //      "    --ssd_filename=<s>      Name of the Filesystem ssd file\n"
  //      "\n";
}

static cxxopts::ParseResult parse_options(int argc, char **argv) {
  cxxopts::Options opt_parser(argv[0]);
  opt_parser.add_options()("h,help", "Print help")(
      "hdd_filename", "Filesystem hdd path", cxxopts::value<std::string>())(
      "ssd_filename", "Filesystem ssd path", cxxopts::value<std::string>());
  opt_parser.allow_unrecognised_options();
  auto options = opt_parser.parse(argc, argv);

  if (options.count("help")) {
    print_usage(argv[0]);
    auto help = opt_parser.help();
    std::cout << "fs_options:"
              << help.substr(help.find("\n\n") + 1, std::string::npos);
    exit(0);
  }

  // Set HDD SSD disk file
  fs.hdd_path = options["hdd_filename"].as<std::string>();
  fs.ssd_path = options["ssd_filename"].as<std::string>();
  LOG(INFO) << "hdd_filename: " << fs.hdd_path << std::endl;
  LOG(INFO) << "ssd_filename: " << fs.ssd_path << std::endl;

  return options;
}

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
};

int main(int argc, char *argv[]) {
  google::InitGoogleLogging(argv[0]);

  // Parse command line options
  auto options{parse_options(argc, argv)};

  // open disk file
  GET_INSTANCE(DiskManager).disk_open(fs.ssd_path, fs.hdd_path);

  // Initialize fuse argument
  fuse_args args = FUSE_ARGS_INIT(0, nullptr);
  fuse_opt_add_arg(&args, argv[0]);
  for (auto &c : options.unmatched()) {
    if (fuse_opt_add_arg(&args, c.c_str())) {
      LOG(FATAL) << "Invalid argument: " << c;
    }
  }

  return fuse_main(args.argc, args.argv, &fs_ops, NULL);
}