/* 
 Moduletest 2
 Test program for 2nd mandatory assignment.
 Tests if a device can be opened in writing mode by multiple threads. 
 It should return "Device or resource busy" after opening the second device.
 */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

int main(int argc, char *argv[])
{
    int fd;
    int fd2;

        fd = open("/dev/dm510-0", O_WRONLY);
        perror("w open");
        fd2 = open("/dev/dm510-0", O_WRONLY);
	    perror("w open");	


    return 0;
}
