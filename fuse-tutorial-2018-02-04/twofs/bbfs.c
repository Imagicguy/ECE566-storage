/*
  Big Brother File System
  Copyright (C) 2012 Joseph J. Pfeiffer, Jr., Ph.D. <pfeiffer@cs.nmsu.edu>

  This program can be distributed under the terms of the GNU GPLv3.
  See the file COPYING.

  This code is derived from function prototypes found /usr/include/fuse/fuse.h
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  His code is licensed under the LGPLv2.
  A copy of that code is included in the file fuse.h
  
  The point of this FUSE filesystem is to provide an introduction to
  FUSE.  It was my first FUSE filesystem as I got to know the
  software; hopefully, the comments in this code will help people who
  follow later to get a gentler introduction.

  This might be called a no-op filesystem:  it doesn't impose
  filesystem semantics on top of any other existing structure.  It
  simply reports the requests that come in, and passes them to an
  underlying filesystem.  The information is saved in a logfile named
  bbfs.log, in the directory from which you run bbfs.
*/
#include "config.h"
#include "params.h"

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

#ifdef HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#include "log.h"

struct stat sourceStatbuf;
char *sourcedir;
static void bb_fullpath(char fpath[PATH_MAX], const char *path)
{
    strcpy(fpath, BB_DATA->rootdir);
    strncat(fpath, path, PATH_MAX); // ridiculously long paths will
				    // break here

    log_msg("    bb_fullpath:  rootdir = \"%s\", path = \"%s\", fpath = \"%s\"\n",
	    BB_DATA->rootdir, path, fpath);
}


int bb_getattr(const char *path, struct stat *statbuf)
{
    int retstat;
    char fpath[PATH_MAX];
    log_msg("\nbb_getattr(path=\"%s\", statbuf=0x%08x)\n",
		path, statbuf);
    bb_fullpath(fpath, path);

    memset(statbuf, 0, sizeof(struct stat));
    if (strstr(fpath, sourcedir) != NULL) {
      if (strstr(fpath,"/file1") != NULL) {
	statbuf->st_mode = S_IFREG | 0666;
	statbuf->st_nlink = 1;
	statbuf->st_size = sourceStatbuf.st_size / 2;
	statbuf->st_ctime = 0;
	statbuf->st_blocks = 4;
	return 0;
      }
      
      if (strstr(fpath, "/file2") != NULL) {
	statbuf->st_mode = S_IFREG | 0666;
	statbuf->st_nlink = 1;
	statbuf->st_size = sourceStatbuf.st_size - sourceStatbuf.st_size / 2;
	statbuf->st_ctime = 0;
	statbuf->st_blocks = 4;
	return 0;
      }
      statbuf->st_mode = S_IFDIR | 0755;
      statbuf->st_nlink = 2;
      return 0;
    }
    retstat = log_syscall("lstat", lstat(fpath, statbuf), 0);
    
    log_stat(statbuf);
    
    return retstat;
}

/** Change the size of a file */
int bb_truncate(const char *path, off_t newsize)
{
    char fpath[PATH_MAX];
    log_msg("\nbb_truncate(path=\"%s\", newsize=%lld)\n",
	    path, newsize);
    bb_fullpath(fpath, path);
    
    if (strstr(fpath,sourcedir) != NULL) {
      return 0;
    }

    return log_syscall("truncate", truncate(fpath, newsize), 0);
}

int bb_open(const char *path, struct fuse_file_info *fi)
{
  int retstat = 0;
  int fd;
  char fpath[PATH_MAX];

  log_msg("\nbb_open(path\"%s\", fi=0x%08x)\n",
	  path, fi);
  bb_fullpath(fpath, path);
  char *sourcedirdup = strdup(sourcedir);
  if (strstr(fpath,strcat(sourcedirdup,"/file1")) != NULL) {
    fd = open(BB_DATA->rootdir,fi->flags);
    log_msg("open return fd is; %d\n",fd);
    fi->fh = fd;
    return 0;
  }
  sourcedirdup = strdup(sourcedir);
  if (strstr(fpath,strcat(sourcedirdup,"/file2")) != NULL) {
    fd = open(BB_DATA->rootdir,fi->flags);
    log_msg("open return fd is; %d\n",fd);
    fi->fh = fd;
    return 0;
  }
  // if the open call succeeds, my retstat is the file descriptor,
  // else it's -errno.  I'm making sure that in that case the saved
  // file descriptor is exactly -1.
  
  retstat = log_error("open");
	   
  return retstat;
}


int bb_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int retstat = 0;
    log_msg("\nbb_read(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
	    path, buf, size, offset, fi);
    log_fi(fi);
    char fpath[PATH_MAX];
    bb_fullpath(fpath,path);
    
    if (strstr(fpath,"/file2") != NULL){
      return log_syscall("pread", pread(fi->fh, buf, size, offset + sourceStatbuf.st_size / 2), 0);
    }
    
    return log_syscall("pread", pread(fi->fh, buf, size, offset), 0);
}


int bb_write(const char *path, const char *buf, size_t size, off_t offset,
	     struct fuse_file_info *fi)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    char* sourcedirdup = strdup(sourcedir);
    bb_fullpath(fpath,path);
    log_msg("fd inside bb_write is %d\n",fi->fh);
    log_msg("\nbb_write(path=\"%s\", buf=0x%08x, size=%d, offset=%lld, fi=0x%08x)\n",
	    path, buf, size, offset, fi
	    );

    log_fi(fi);
    printf("size in write is %ld\n",size);
    if (size > sourceStatbuf.st_size / 2) {
      printf("size in write is %ld\n",size);
      return -1;
    }
    
    if (strstr(fpath,strcat(sourcedirdup,"/file2")) != NULL){
      log_msg("someone wants to write in file1 !\n");
      return log_syscall("pwrite", pwrite(fi->fh, buf, size, offset + sourceStatbuf.st_size / 2), 0);
    }


    return log_syscall("pwrite", pwrite(fi->fh, buf, size, offset), 0);
}

/** Get file system statistics
 *
 * The 'f_frsize', 'f_favail', 'f_fsid' and 'f_flag' fields are ignored
 *
 * Replaced 'struct statfs' parameter with 'struct statvfs' in
 * version 2.5
 */
int bb_statfs(const char *path, struct statvfs *statv)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_msg("\nbb_statfs(path=\"%s\", statv=0x%08x)\n",
	    path, statv);
    bb_fullpath(fpath, path);
    
    // get stats for underlying filesystem
    retstat = log_syscall("statvfs", statvfs(fpath, statv), 0);
    
    log_statvfs(statv);
    
    return retstat;
}


#ifdef HAVE_SYS_XATTR_H

/** Get extended attributes */
int bb_getxattr(const char *path, const char *name, char *value, size_t size)
{
  int retstat = 0;
  char fpath[PATH_MAX];
  char * sourcedirdup = strdup(sourcedir);
  log_msg("\nbb_getxattr(path = \"%s\", name = \"%s\", value = 0x%08x, size = %d)\n",
	  path, name, value, size);
  bb_fullpath(fpath, path);
  if (strstr(fpath, sourcedirdup) != NULL)
    {
      return 0;                                                                                   
    }   
  
  retstat = log_syscall("lgetxattr", lgetxattr(fpath, name, value, size), 0);
  if (retstat >= 0)
    log_msg("    value = \"%s\"\n", value);
  
  return retstat;
}

#endif
int bb_opendir(const char *path, struct fuse_file_info *fi)
{
    DIR *dp;
    int retstat = 0;
    char fpath[PATH_MAX];
    
    log_msg("\nbb_opendir(path=\"%s\", fi=0x%08x)\n",
	  path, fi);
    bb_fullpath(fpath, path);
    if (strstr(fpath, sourcedir) != NULL)
      {
	return 0;                                                                                   
      }
    // since opendir returns a pointer, takes some custom handling of
    // return status.
    dp = opendir(fpath);
    log_msg("    opendir returned 0x%p\n", dp);
    if (dp == NULL)
	retstat = log_error("bb_opendir opendir");
    
    fi->fh = (intptr_t) dp;
    
    log_fi(fi);
    
    return retstat;
}

int bb_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
	       struct fuse_file_info *fi)
{
    int retstat = 0;
    DIR *dp;
    char fpath[PATH_MAX];
    struct dirent *de;
    log_msg("\nbb_readdir(path=\"%s\", buf=0x%08x, filler=0x%08x, offset=%lld, fi=0x%08x)\n",
		path, buf, filler, offset, fi);
    bb_fullpath(fpath,path);
    if (strstr(fpath, sourcedir) != NULL)
      {
	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	filler(buf, "file1", NULL, 0);
	filler(buf, "file2", NULL, 0);
	return (0);
      }
}


void *bb_init(struct fuse_conn_info *conn)
{
    log_msg("\nbb_init()\n");
    
    log_conn(conn);
    log_fuse_context(fuse_get_context());
    
    return BB_DATA;
}

int bb_access(const char *path, int mask)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    if (strcmp(path,""))
    log_msg("\nbb_access(path=\"%s\", mask=0%o)\n",
	    path, mask);
    bb_fullpath(fpath, path);

    if (strstr(fpath,sourcedir) != NULL)
      {
	return 0;                                                                                   
      }   

    retstat = access(fpath, mask);
    
    if (retstat < 0)
	retstat = log_error("bb_access access");
    
    return retstat;
}


int bb_fgetattr(const char *path, struct stat *statbuf, struct fuse_file_info *fi)
{
    int retstat = 0;
    char fpath[PATH_MAX];
    log_msg("\nbb_fgetattr(path=\"%s\", statbuf=0x%08x, fi=0x%08x)\n",
	    path, statbuf, fi);
    log_fi(fi);
    bb_fullpath(fpath, path);
    if (strstr(fpath, sourcedir) != NULL) {
      if (strstr(fpath,"/file1") != NULL) {
	log_msg("go into getattr file1\n");
	statbuf->st_mode = S_IFREG | 0666;
	statbuf->st_nlink = 1;
	statbuf->st_size = sourceStatbuf.st_size / 2;
	statbuf->st_ctime = 0;
	statbuf->st_blocks = 4;
	return 0;
      }
      
      if (strstr(fpath, "/file2") != NULL) {
	statbuf->st_mode = S_IFREG | 0666;
	statbuf->st_nlink = 1;
	statbuf->st_size = sourceStatbuf.st_size - sourceStatbuf.st_size / 2;
	statbuf->st_ctime = 0;
	statbuf->st_blocks = 4;
	return 0;
      }
      statbuf->st_mode = S_IFDIR | 0755;
      statbuf->st_nlink = 2;
      return 0;
    }

    if (!strcmp(path, "/"))
	return bb_getattr(path, statbuf);
    
    retstat = fstat(fi->fh, statbuf);
    if (retstat < 0)
	retstat = log_error("bb_fgetattr fstat");
    
    log_stat(statbuf);
    
    return retstat;
}

struct fuse_operations bb_oper = {
  .getattr = bb_getattr,
  .truncate = bb_truncate,
  .open = bb_open,
  .read = bb_read,
  .write = bb_write,
    
#ifdef HAVE_SYS_XATTR_H
  .getxattr = bb_getxattr,
#endif
  .opendir = bb_opendir,
  .readdir = bb_readdir,  
  .access = bb_access,
  .fgetattr = bb_fgetattr
};

int main(int argc, char *argv[])
{
    int fuse_stat;
    struct bb_state *bb_data;
    bb_data = malloc(sizeof(struct bb_state));

    if (bb_data == NULL) {
      perror("main calloc");
      abort();
    }
    
    bb_data->rootdir = realpath(argv[argc-2], NULL);
    argv[argc-2] = argv[argc-1];
    argv[argc-1] = NULL;
    argc--;
    
    sourcedir = strdup(bb_data->rootdir);
    printf("%s\n",sourcedir);
    
    memset(&sourceStatbuf, 0, sizeof(stat));
    if (lstat(sourcedir,&sourceStatbuf) != 0) {
      perror("Failed to get stat struct for source dir\n");
    }
    printf("image size is %ld\n",(off_t)sourceStatbuf.st_size);
    

    
    
    
    bb_data->logfile = log_open();
    
    fuse_stat = fuse_main(argc, argv, &bb_oper, bb_data);
   
    
    return fuse_stat;
}
