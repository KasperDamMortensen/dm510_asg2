/* Test program for 2nd mandatory assignment.
    Test if ioctl can adjust the buffer size.
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
    int fd;
    unsigned long i = 15;
    // struct dm510_device *dev;
    // int bufSize;
    int ret;
    int val1 = 1;
    int val2 = 2;
    int val3 = 3;
    int rec1;
    int rec2;
    int rec3;
    int count = 1;

    fd = open("/dev/dm510-0", O_RDWR);
    perror("open1");

    ret = write(fd, &val1, count);
    if (ret == -1) {
        perror("write");
        exit(1);
    }
    ret = write(fd, &val2, count);
    if (ret == -1) {
        perror("write");
        exit(1);
    }
    ret = write(fd, &val3, count);
    if (ret == -1) {
        perror("write");
        exit(1);
    }
    
    // printf("Amount written: %d\n", ret);

    fd = open("/dev/dm510-1", O_RDWR);
    perror("open2");

    ret = read(fd, &rec1, count);
    if (ret == -1) {
        perror("read");
        exit(1);
    }
    ret = read(fd, &rec2, count);
    if (ret == -1) {
        perror("read");
        exit(1);
    }
    ret = read(fd, &rec3, count);
    if (ret == -1) {
        perror("read");
        exit(1);
    }

    // printf("Amount read: %d\n", ret);
    printf("Message read: %d\n", rec1);   
    printf("Message read: %d\n", rec2);   
    printf("Message read: %d\n", rec3);   

    return 0;
}
