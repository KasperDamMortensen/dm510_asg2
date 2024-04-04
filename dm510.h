/* Prototype module for second mandatory DM510 assignment */
#ifndef __KERNEL__
#  define __KERNEL__
#endif
    #ifdef __KERNEL__
    /* This one if debugging is on, and kernel space */
# define PDEBUG(fmt, args...) printk( KERN_DEBUG "dm510: " fmt, ## args)
#endif
#ifndef MODULE
#  define MODULE
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>	
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/wait.h>
/* #include <asm/uaccess.h> */
#include <linux/uaccess.h>
#include <linux/semaphore.h>
/* #include <asm/system.h> */
#include <asm/switch_to.h>
#include <linux/cdev.h>
#include <linux/ioctl.h>

/* Prototypes - this would normally go in a .h file */
struct dm510_device; // is this needed???
struct buffer;
static int dm510_open( struct inode*, struct file* );
static int dm510_release( struct inode*, struct file* );
static ssize_t dm510_read( struct file*, char*, size_t, loff_t* );
static ssize_t dm510_write( struct file*, const char*, size_t, loff_t* );
long dm510_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

int setup_buffer(struct buffer **buf, int buffer_size);
static void setup_cdev(struct dm510_device *dev, int index);
static int spacefree(struct buffer *buf);
static int getwritespace(struct buffer *buf, struct file *filp);

#define DEVICE_NAME "dm510_dev" /* Dev name as it appears in /proc/devices */
#define MAJOR_NUMBER 255
#define MIN_MINOR_NUMBER 0
#define MAX_MINOR_NUMBER 1

#define DEVICE_COUNT 2
#define BUFFER_SIZE 10	// This number should probably be changed later
#define MAX_READERS 10  // This number should probably be changed later

/*
* Ioctl definitions
*/

/* Use 'Z' as magic number */
#define DM510_IOC_MAGIC 'Z'
#define DM510_IOC_MAXNR 2

#define DM510_IOC_ADJUST_BUFFERSIZE  _IOW(DM510_IOC_MAGIC, 1, unsigned long)
#define DM510_IOC_ADJUST_NREADERS    _IOW(DM510_IOC_MAGIC, 2, unsigned long)


/* end of what really should have been in a .h file */



