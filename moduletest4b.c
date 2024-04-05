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
    int fd;
    int val = 0;
    int res = 1;
    int i;

    fd = open("/dev/dm510-0", O_RDWR);
    perror("WR open");

    ioctl(fd, DM510_IOC_ADJUST_BUFFERSIZE, 5);
    perror("ioctl:");

    for (i = 0; i < 15; i++) {   
        val++;
        res = write(fd, &val, 1);
        printf("result: %d\n", res);
        printf("Wrote value1: %d\n", val);
    }

    return 0;
}
