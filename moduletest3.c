/* Test program for 2nd mandatory assignment.
    test if ioctl can increase the maximum number of readers
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#include <errno.h>

#define DM510_IOC_MAGIC 'Z'
#define DM510_IOC_MAXNR 2

#define DM510_IOC_ADJUST_BUFFERSIZE  _IOW(DM510_IOC_MAGIC, 1, unsigned long)
#define DM510_IOC_ADJUST_NREADERS    _IOW(DM510_IOC_MAGIC, 2, unsigned long)


int main(int argc, char *argv[])
{
    pid_t pid;
    int fd;
    int fd2;
    int fd3;
    int sum = 0;
    int val;
    int cnt;
    unsigned long i = 2;

        fd = open("/dev/dm510-0", O_RDWR);
        perror("open1:");
        fd2 = open("/dev/dm510-0", O_RDONLY);
        perror("open2:");
        if (ioctl(fd, DM510_IOC_ADJUST_NREADERS, i) < 0) {
            perror("ioctl failed");
            return 1;
        }
        printf("set the maximum number of readers to %lu\n ",i);
        fd3 = open("/dev/dm510-0", O_RDONLY);
        perror("open3:");
    return 0;
}
