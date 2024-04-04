/*
tests if the ioctl function can change the buffer size

*/


#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

#define DM510_IOC_MAGIC 'Z'
#define DM510_IOC_MAXNR 2

#define DM510_IOC_ADJUST_BUFFERSIZE  _IOW(DM510_IOC_MAGIC, 1, unsigned long)
#define DM510_IOC_ADJUST_NREADERS    _IOW(DM510_IOC_MAGIC, 2, unsigned long)


int main(int argc, char *argv[])
{
    pid_t pid;
    int fd,fd2;
    int val=0;
    int res;
    unsigned long n =2;
    unsigned long m =4;
    

    fd = open("/dev/dm510-0", O_RDWR);
    perror("WR open");
    fd2 = open("/dev/dm510-1", O_RDONLY);
    perror("WR open");
    ioctl(fd2, DM510_IOC_ADJUST_BUFFERSIZE, n);
    res = write(fd,&val,1);
    if (res <= 0){
        printf("buffer full\n");
    }
    res = write(fd,&val,1);
    if (res <= 0){
        printf("buffer full\n");
    }
    ioctl(fd, DM510_IOC_ADJUST_BUFFERSIZE, m);
    res = write(fd,&val,2);
    if (res >= 0){
        printf("added value: %d after increasing buffersize\n",val);
    }

    close(fd);
    return 0;
}
