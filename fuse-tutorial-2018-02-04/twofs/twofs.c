#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "twofs.h"

#define FUSE_USE_VERSION 21


static int fs_readdir(const char *path, void *data, fuse_fill_dir_t filler,
    off_t off, struct fuse_file_info *ffi)
{
  if (strcmp(path, "/") != 0)
    return (-ENOENT);

  filler(data, ".", NULL, 0);
  filler(data, "..", NULL, 0);
  filler(data, "file1", NULL, 0);
  filler(data, "file2", NULL, 0);
  return (0);
}
/*
static int fs_read(const char *path, char *buf, size_t size, off_t off,
    struct fuse_file_info *ffi)
{
	if (off >= 5)
		return (0);

	size = 5 - off;
	memcpy(buf, "data." + off, size);
	return (size);
}
*/
static int fs_open(const char *path, struct fuse_file_info *ffi)
{
  printf("inside fs_open: %s", path);
  if ((strcmp(path, "/file1") != 0) && (strcmp(path, "/file2") != 0 )) {
      return (-ENOENT);
  }
  
  
  if ((ffi->flags & 3) != O_RDONLY)
    return (-EACCES);
  
  return (0);
}

static int fs_getattr(const char *path, struct stat *statbuf)
{ 
  if (strcmp(path,"/file1") == 0) {
    printf("comes in bb_getattr jump!\n");
    statbuf->st_mode = S_IFREG | 0666;
    statbuf->st_nlink = 1;
    statbuf->st_size = 1024;;
    statbuf->st_ctime = 0;
    return 0;
  } else if (strcmp(path,"/file2") == 0) {
    printf("comes in bb_getattr jump!\n");
    statbuf->st_mode = S_IFREG | 0666;
    statbuf->st_nlink = 1;
    statbuf->st_size = 1024;
    statbuf->st_ctime = 0;
    return 0;
  } else {
    return (-ENOENT);
  }
  
}

struct fuse_operations fsops = {
	.readdir = fs_readdir,
	//	.read = fs_read,
	.open = fs_open,
	.getattr = fs_getattr,
};

int main(int ac, char *av[])
{
	return (fuse_main(ac, av, &fsops, NULL));
}
