/*
 * RAID1 example for BUSE
 * by Tyler Bletsch to ECE566, Duke University, Fall 2019
 * 
 * Based on 'busexmp' by Adam Cozzette
 *
 * This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 */
 
#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE

#include <argp.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>

#include "buse.h"

#define UNUSED(x) (void)(x) // used to suppress "unused variable" warnings without turning off the feature entirely

int dev_fd[16]; // file descriptors for two underlying block devices that make up the RAID
int block_size;  //NOTE: other than truncating the resulting raid device, block_size is ignored in this program; it is asked for and set in order to make it easier to adapt this code to RAID0/4/5/6.
uint64_t raid_device_size; // size of raid device in bytes
bool verbose = false;  // set to true by -v option for debug output
bool degraded = false; // true if we're missing a device
int missing_dev = -1;//
int dev_num = 0;// include parity drive
int ok_dev = -1; // index of dev_fd that has a valid drive (used in degraded mode to identify the non-missing drive (0 or 1))
int rebuild_dev = -1; // index of drive that is being added with '+' for RAID rebuilt

int last_read_dev = 0; // used to interleave reading between the two devices

static int xmp_read(void *buf, u_int32_t len, u_int64_t offset, void *userdata) {
  UNUSED(userdata);
  if (verbose)
    fprintf(stderr, "R - %lu, %u\n", offset, len);
  
  // for the missing drive, retrive data from parity calculation 
  int currentDev;
  int diskOffset;
  while (len > 0) {
    //add 1 to ensure parity drive would not be effected
    currentDev  = 1 + (offset % ((dev_num - 1) * block_size)) / block_size;
    diskOffset = ((offset / block_size) / (dev_num - 1)) * block_size + (offset % block_size);

    if(dev_fd[currentDev] == -1) {
      //retrive data from parity calculation
      char xor[block_size];
      char tmp[block_size];
      int r;
      for (int i = 0;i < dev_num;i++) {
	if (i == currentDev) {
	  continue;
	}
	r = pread(dev_fd[i],tmp,block_size,diskOffset);
	if (r<0) {
	  perror("xmp_read(degraded)");
	  return -1;
	} else if (r != block_size) {
	  fprintf(stderr, "xmp_read(degraded) short read \n");
	  return 1;
	}
	for (int j = 0;j < block_size;j++) {
	  xor[j] = (char) (xor[j] ^ tmp[j]);
	}
      }
      memcpy(buf,xor,block_size);
      len -= block_size;
      buf += block_size;
      offset += block_size;
    } else {// no missing drive, just do regular read
      if (pread(dev_fd[currentDev],buf, block_size, diskOffset) == -1) {
	perror("xmp_read error: return value -1\n");
      }else {
	len -= block_size;
	buf += block_size;
	offset += block_size;
      }
    }
	
   
  }
  return 0;
}
  static int xmp_write(const void *buf, u_int32_t len, u_int64_t offset, void *userdata) {
    UNUSED(userdata);
    if (verbose)
      fprintf(stderr, "W - %lu, %u\n", offset, len);
    // write to both drives
    int currentDev;
    int diskOffset;
    fprintf(stderr, "xmp_write: len %d offset %ld. \n", len, offset);
    while (len > 0) {
      currentDev  = 1 + (offset % ((dev_num - 1) * block_size)) / block_size;
     
      diskOffset = ((offset / block_size) / (dev_num - 1)) * block_size + (offset % block_size);
      char old_block[block_size];// store old value of changing block for parity re-calculation
      if (pread(dev_fd[currentDev],old_block,block_size,diskOffset) != block_size) {
	perror("xmp_write(degraded) short read");
      }
      char parity_block[block_size];
      if (pread(dev_fd[0],old_block,block_size,diskOffset) != block_size){
	perror("xmp_write(degraded) short parity read");
      }
      char new_block[block_size];
      memcpy(new_block,buf,block_size);
      for (int i = 0; i < block_size;i++) {
	parity_block[i] ^= old_block[i] ^ new_block[i];
      }
      if (pwrite(dev_fd[0],parity_block, block_size, diskOffset) == -1) {
	perror("xmp_write error: parity write return value -1\n");
      }
      
      if (dev_fd[currentDev] == -1) {//means this drive is missing
	continue;
      }
      
      if (pwrite(dev_fd[currentDev],buf, block_size, diskOffset) == -1) {
	perror("xmp_write error: return value -1\n");
      }else {
	len -= block_size;
	buf += block_size;
	offset += block_size;
      }
    }
    
    return 0;
}

static int xmp_flush(void *userdata) {
    UNUSED(userdata);
    if (verbose)
        fprintf(stderr, "Received a flush request.\n");
    for (int i=0; i<2; i++) {
        if (dev_fd[i] != -1) { // handle degraded mode
            fsync(dev_fd[i]); // we use fsync to flush OS buffers to underlying devices
        }
    }
    return 0;
}

static void xmp_disc(void *userdata) {
    UNUSED(userdata);
    if (verbose)
        fprintf(stderr, "Received a disconnect request.\n");
    // disconnect is a no-op for us
}

/*
// we'll disable trim support, you can add it back if you want it
static int xmp_trim(u_int64_t from, u_int32_t len, void *userdata) {
    UNUSED(userdata);
    if (verbose)
        fprintf(stderr, "T - %lu, %u\n", from, len);
    // trim is a no-op for us
    return 0;
}
*/

/* argument parsing using argp */

static struct argp_option options[] = {
    {"verbose", 'v', 0, 0, "Produce verbose output", 0},
    {0},
};

struct arguments {
    uint32_t block_size;
    char* device[16];
    char* raid_device;
    int verbose;
};

/* Parse a single option. */
static error_t parse_opt(int key, char *arg, struct argp_state *state) {
    struct arguments *arguments = state->input;
    char * endptr;

    switch (key) {

        case 'v':
            arguments->verbose = 1;
            break;

        case ARGP_KEY_ARG:
            switch (state->arg_num) {

                case 0:
                    arguments->block_size = strtoul(arg, &endptr, 10);
                    if (*endptr != '\0') {
                        /* failed to parse integer */
                        errx(EXIT_FAILURE, "SIZE must be an integer");
                    }
                    break;

                case 1:
                    arguments->raid_device = arg;
                    break;

                case 2:
                    arguments->device[0] = arg;
		    dev_num++;
                    break;

                case 3:
                    arguments->device[1] = arg;
		    dev_num++;
                    break;

		case 4:
                    arguments->device[2] = arg;
		    dev_num++;
                    break;
		    
                case 5:
                    arguments->device[3] = arg;
		    dev_num++;
		    break;
		    
                case 6:
                    arguments->device[4] = arg;
		    dev_num++;
                    break;
		    
                case 7:
                    arguments->device[5] = arg;
		    dev_num++;
                    break;
		    
                case 8:
                    arguments->device[6] = arg;
		    dev_num++;
                    break;
		    
                case 9:
                    arguments->device[7] = arg;
		    dev_num++;
                    break;
		    
                case 10:
                    arguments->device[8] = arg;
		    dev_num++;
                    break;
		    
                case 11:
                    arguments->device[9] = arg;
		    dev_num++;
                    break;
		    
                case 12:
                    arguments->device[10] = arg;
		    dev_num++;
		    break;
		    
                case 13:
                    arguments->device[11] = arg;
		    dev_num++;
                    break;
		    
                case 14:
                    arguments->device[12] = arg;
		    dev_num++;
                    break;
		case 15:
                    arguments->device[13] = arg;
		    dev_num++;
                    break;
                case 16:
                    arguments->device[14] = arg;
		    dev_num++;
                    break;
                case 17:
                    arguments->device[15] = arg;
		    dev_num++;
                    break;
                
                default:
                    /* Too many arguments. */
                    return ARGP_ERR_UNKNOWN;
            }
            break;

        case ARGP_KEY_END:
            if (state->arg_num < 4) {
                warnx("not enough arguments");
                argp_usage(state);
            }
            break;

        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = {
    .options = options,
    .parser = parse_opt,
    .args_doc = "BLOCKSIZE RAIDDEVICE DEVICE1 DEVICE2",
    .doc = "BUSE implementation of RAID1 for two devices.\n"
           "`BLOCKSIZE` is an integer number of bytes. "
           "\n\n"
           "`RAIDDEVICE` is a path to an NBD block device, for example \"/dev/nbd0\"."
           "\n\n"
           "`DEVICE*` is a path to underlying block devices. Normal files can be used too. A `DEVICE` may be specified as \"MISSING\" to run in degraded mode. "
           "\n\n"
           "If you prepend '+' to a DEVICE, you are re-adding it as a replacement to the RAID, and we will rebuild the array. "
           "This is synchronous; the rebuild will have to finish before the RAID is started. "
};

static int do_raid_rebuild() {
    // target drive index is: rebuild_dev
  char buf[block_size];

  for (int i = 0;i < dev_num;i++) {
    lseek(dev_fd[i],0,SEEK_SET);//set all device offset to head of file
  }
  
  int r;
  for (uint64_t cursor=0; cursor<raid_device_size; cursor+=block_size) {
    char xor[block_size];
    for (int i = 0;i < dev_num;i++) {
      if (i == rebuild_dev) {
	continue;
      }
      r = read(dev_fd[i],buf,block_size);
      if (r<0) {
	perror("rebuild_read");
	return -1;
      } else if (r != block_size) {
	fprintf(stderr, "rebuild_read: short read (%d bytes), offset=%zu\n", r, cursor);
	return 1;
      }
      for (int j = 0;j < block_size;j++) {
	xor[j] = (char) (xor[j] ^ buf[j]);
      }
    }
    r = write(dev_fd[rebuild_dev],xor,block_size);
    if (r<0) {
      perror("rebuild_write");
      return -1;
    } else if (r != block_size) {
      fprintf(stderr, "rebuild_write: short write (%d bytes), offset=%zu\n", r, cursor);
      return 1;
    }
  }
  return 0;
}
  


int main(int argc, char *argv[]) {
    struct arguments arguments = {
        .verbose = 0,
	.device = "",
    };
    argp_parse(&argp, argc, argv, 0, 0, &arguments);
    
    struct buse_operations bop = {
        .read = xmp_read,
        .write = xmp_write,
        .disc = xmp_disc,
        .flush = xmp_flush,
        // .trim = xmp_trim, // we'll disable trim support, you can add it back if you want it
    };

    verbose = arguments.verbose;
    block_size = arguments.block_size;
    
    raid_device_size=0; // will be detected from the drives available
    ok_dev=-1;
    bool rebuild_needed = false; // will be set to true if a drive is MISSING
    fprintf(stderr,"Device number is %d!\n",dev_num);
    for (int i=0; i< 16; i++) {
      char* dev_path = arguments.device[i];
      fprintf(stderr,"device %d name is : %s\n",i,dev_path);       
      if (dev_path  == 0) {
	fprintf(stderr,"device %d doesn't exist\n",i);
	continue;// which means this device does not exist
      }
      if (strcmp(dev_path,"MISSING")==0) {
	missing_dev++;
	degraded = true;
	dev_fd[i] = -1;//set to -1 when this device is missing.
	fprintf(stderr, "DEGRADED: Device number %d is missing!\n", i);
      } else {
	if (dev_path[0] == '+') { // RAID rebuild mode!!
	  if (rebuild_needed) {
	    // multiple +drives detected
	    fprintf(stderr, "ERROR: Multiple '+' drives specified. Can only recover one drive at a time.\n");
	    exit(1);
	  }
	  dev_path++; // shave off the '+' for the subsequent logic
	  rebuild_dev = i;
	  rebuild_needed = true;
	}

	dev_fd[i] = open(dev_path,O_RDWR);
	if (dev_fd[i] < 0) {
	  perror(dev_path);
	  exit(1);
	}
	uint64_t size = lseek(dev_fd[i],0,SEEK_END); // used to find device size by seeking to end
	fprintf(stderr, "Got device '%s', size %ld bytes.\n", dev_path, size);
	if (raid_device_size==0 || size<raid_device_size) {
	  raid_device_size = size; // raid_device_size is minimum size of available devices
	}
      }
    }
    
    raid_device_size = raid_device_size/block_size*block_size; // divide+mult to truncate to block size
    bop.size = raid_device_size; // tell BUSE how big our block device is
    if (rebuild_needed) {
      if (missing_dev > 1) {
	fprintf(stderr,"ERROR: missed device number > 1. Can't rebuild anymore.\n");
	exit(1);
      }
      if (degraded) {
	fprintf(stderr, "ERROR: Can't rebuild when a device is missing(i.e., you can't combine MISSING and '+').\n");
	exit(1);
      }
      fprintf(stderr, "Doing RAID rebuild...\n");
      if (do_raid_rebuild() != 0) { 
	// error on rebuild
	fprintf(stderr, "Rebuild failed, aborting.\n");
	exit(1);
      }else {
	missing_dev--;
      }
    }
    fprintf(stderr, "RAID device resulting size: %ld.\n", bop.size);
    
    return buse_main(arguments.raid_device, &bop, NULL);
}
