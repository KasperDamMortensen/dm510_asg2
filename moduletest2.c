/* Test program for 2nd mandatory assignment.
 tests if a device can be opened in writing mode by multiple threads. 
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#define ITS 10000


int main(int argc, char *argv[])
{
    pid_t pid;
    int fd3;
    int fd2;
    int sum = 0, i;
    int val;
    int cnt;

        fd3 = open("/dev/dm510-0", O_WRONLY);
        perror("w open");
        fd2 = open("/dev/dm510-0", O_WRONLY);
	    perror("w open");	


    return 0;
}
