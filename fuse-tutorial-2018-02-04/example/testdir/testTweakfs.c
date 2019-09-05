#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

int main(int argc, char* argv[]) {
  int fd;
  char buf[1024];
  int ret;
  if (2 != argc) {
    printf("\n Invalid Input \n");
  }

  errno = 0;
  fd = open(argv[1],O_RDONLY);

  if(-1 == fd)
  {
    printf("\n open() failed with error [%s]\n",strerror(errno));
    return 1;
  }else {
    printf("\n Open() Successful\n");
    while ((ret = read(fd, buf, sizeof(buf)-1)) > 0) {
      buf[ret] = 0x00;
      printf("block read: \n %s \n ", buf);
    }
    close(fd);
  }
  
}
