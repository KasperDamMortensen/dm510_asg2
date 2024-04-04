/* Test program for 2nd mandatory assignment.
 tests if a device can be opened in reading mode by multiple threads.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#define ITS 10000

void read_all(int fd, void *buf, int count) {
    while (count > 0) {
        int ret;
        ret = read(fd, buf, count);
        if (ret == -1) {
            perror("read");
            exit(1);
        }

        count -= ret;
        buf += ret;
    }
}

void write_all(int fd, void *buf, int count) {
    while (count > 0) {
        int ret;
        ret = write(fd, buf, count);
        if (ret == -1) {
            perror("write");
            exit(1);
        }

        count -= ret;
        buf += ret;
    }
}

int main(int argc, char *argv[])
{
    pid_t pid;
    int fd;
    int fd2;
    int sum = 0, i;
    int val;
    int cnt;

        fd = open("/dev/dm510-0", O_WRONLY);
        perror("w open");
        fd2 = open("/dev/dm510-0", O_WRONLY);
	    perror("w open");	


    return 0;
}
