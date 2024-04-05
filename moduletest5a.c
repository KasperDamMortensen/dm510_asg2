/* 
 Moduletest 5a
 Test program for 2nd mandatory assignment.
 Checks and shows that the buffersize is indeed 10 when the module is loaded.
 It should print a count to 9 and then sleep and run until stopped manually.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>

int main(int argc, char *argv[])
{
    int fd;
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
