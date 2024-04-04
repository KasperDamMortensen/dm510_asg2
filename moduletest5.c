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
    int fd, fd2;
    int val = 0;
    int res = 1;
    int i;

    fd = open("/dev/dm510-0", O_RDWR);
    perror("WR open");
    fd2 = open("/dev/dm510-1", O_RDWR);
    perror("WR open");

    ioctl(fd2, DM510_IOC_ADJUST_BUFFERSIZE, 10);
    ioctl(fd, DM510_IOC_ADJUST_BUFFERSIZE, 10);
    perror("ioctl:");

    for (i = 0; i < 10; i++) {
        val++;
        res = write(fd, &val, 1);
        printf("result: %d\n", res);
        printf("Wrote value1: %d\n", val);
    }

    for (i = 0; i < 5; i++) {
        read(fd2, &res, 1);
        printf("read value1: %d\n", res);
        if (res != i + 1) {
            printf("Error: Read value1 is not in order!\n");
            return 1; // Exit with error
        }
    }

    val = 0;

    for (i = 10; i < 15; i++) {
        val++;
        res = write(fd, &val, 1);
        printf("Wrote value2: %d\n", val);
    }

    res = 1;
    val = 0;

    for (i = 0; i < 9; i++) {
        res = read(fd2, &val, 1);
        printf("read value2: %d\n", val);
        if (val != i + 1) {
            printf("Error: Read value2 is not in order!\n");
            return 1; // Exit with error
        }
    }

    return 0;
}
