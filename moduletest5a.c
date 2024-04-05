#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

int main(int argc, char *argv[])
{
    pid_t pid;
    int fd, fd2;
    int val = 0;
    int res = 1;
    int i;

    fd = open("/dev/dm510-0", O_RDWR);
    perror("WR open");

    for (i = 0; i < 15; i++) {   
        val++;
        res = write(fd, &val, 1);
        printf("result: %d\n", res);
        printf("Wrote value1: %d\n", val);
    }

    return 0;
}
