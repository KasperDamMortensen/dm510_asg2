/* Prototype module for second mandatory DM510 assignment */
#ifndef __KERNEL__
#  define __KERNEL__
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
/* Prototypes - this would normally go in a .h file */
static int dm510_open( struct inode*, struct file* );
static int dm510_release( struct inode*, struct file* );
static ssize_t dm510_read( struct file*, char*, size_t, loff_t* );
static ssize_t dm510_write( struct file*, const char*, size_t, loff_t* );
long dm510_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);

#define DEVICE_NAME "dm510_dev" /* Dev name as it appears in /proc/devices */
#define MAJOR_NUMBER 255
#define MIN_MINOR_NUMBER 0
#define MAX_MINOR_NUMBER 1

#define DEVICE_COUNT 2
#define BUFFER_SIZE 10	// This number should probably be changed later

/* end of what really should have been in a .h file */






// This is a device which reads from one buffer and writes into another. 
// This is initialised in the open method.
struct device {
	struct buffer bufferIN;			// The buffer it reads from.
	struct buffer bufferOut;		// The buffer it writes to.
    struct cdev cdev;               /* Char device structure */		// Would this mean that buffer doesn't get a cdev? Probably
    struct mutex mutex;             /* mutual exclusion semaphore */		// I think this should be included.
}

// This is a buffer which is accessed by one device for reading and another for writing.
struct buffer {
        wait_queue_head_t inq, outq;       /* read and write queues */
        char *start, *end;                 /* begin of buf, end of buf */		// I renamed it to start.
        int buffersize;                    /* used in pointer arithmetic */
        char *rp, *wp;                     /* where to read, where to write */
        int nreaders, nwriters;            /* number of openings for r/w */
        struct mutex mutex;                /* mutual exclusion semaphore */
};

/* 
 * Our parameters which can be set at load time.
 */

int major_number =   MAJOR_NUMBER;		// scull_major
int minor_number =   0;					// scull_minor
int nr_devs = DEVICE_COUNT;				// number of devices (prev scull_p_nr_devs) 		// static? I saw this somewhere...
int buffer_size =  BUFFER_SIZE;			// Explains itself (prev scull_p_buffer) 
dev_t first_devno;						// Our first device number (prev scull_p_devno)

// I suppose that module_param allows you to modify some structure safely and easily...
module_param(major_number, int, S_IRUGO);
module_param(minor_number, int, S_IRUGO);
module_param(nr_devs, int, S_IRUGO);
module_param(nr_devs, int, 0);	/* FIXME check perms */
module_param(buffer_size, int, 0);

// These are our two devices.
// This variable gets used in the init function.
static struct device *devices;

// These are our two buffers.
static struct buffer *buffer0;
static struct buffer *buffer1;


/* file operations struct */
static struct file_operations dm510_fops = {
	.owner   = THIS_MODULE,
	.read    = dm510_read,
	.write   = dm510_write,
	.open    = dm510_open,
	.release = dm510_release,
    .unlocked_ioctl   = dm510_ioctl
};








// Auxiliary function for initialising a single buffer. 
struct buffer setup_buffer( void ) {

	struct buffer *buf;

	// Allocate the structure
	buf = kmalloc(sizeof(struct buffer), GFP_KERNEL);	
	if (!buf) {

	}
	memset(buf, 0, sizeof(struct buffer));

	// Allocate the actual buffer 
	buf->start = kmalloc(buffer_size, GFP_KERNEL);
	if (!buf->start) {
		return -ENOMEM;
	}

	// Initialise stuff
	init_waitqueue_head(&(buf.inq));
	init_waitqueue_head(&(buf.outq));
	mutex_init(buf.mutex);

	// Initialise the offset pointers
	buf->buffersize = buffer_size;
	buf->end = buf->start + buf->buffersize;
	buf->rp = buf->wp = buf->start; /* rd and wr from the beginning */

	return buf;
}


// Auxiliary function to dm510_init_module
/*
 * Set up a cdev entry.
 */
static void setup_cdev(struct device *dev, int index) {
	int err, devno = first_devno + index;
    
	cdev_init(&dev->cdev, &dm510_fops);
	dev->cdev.owner = THIS_MODULE;
	err = cdev_add (&dev->cdev, devno, 1);
	/* Fail gracefully if need be */
	if (err)
		printk(KERN_NOTICE "Error %d adding buffer%d", err, index);
}



/* called when module is loaded */
int dm510_init_module( void ) {

	/* initialization code belongs here */

	// Here starts the code taken from scull_init_module.
	int result, i;
	dev_t dev = 0;

	// First initialise both buffers.
	buffer0 = setup_buffer();
	buffer1 = setup_buffer();

	// This part of the code makes the device. (No!)
	// This part makes a dev_t, which is a number containing both a major and a minor number. 
	// And it then registers them as belonging to a device. If the chosen major is available, it is used.
	// Otherwise a new one gets assigned by the kernel. 
	/*
 	* Get a range of minor numbers to work with, asking for a dynamic
 	* major unless directed otherwise at load time.
 	*/
	if (major_number) {
		dev = MKDEV(major_number, minor_number);
		result = register_chrdev_region(dev, nr_devs, "DM510"); // I replace "scull" with "DM510"
	} else {
		result = alloc_chrdev_region(&dev, minor_number, nr_devs,
				"DM510");
		major_number = MAJOR(dev);
	}
	if (result < 0) {
		printk(KERN_WARNING "DM510: can't get major %d\n", major_number);
		return result;
	}

	// Here starts the code taken from scull_p_init.
	first_devno = dev;
	devices = kmalloc(nr_devs * sizeof(struct device), GFP_KERNEL);
	if (devices == NULL) {
		scull_cleanup_module();
		return -ENOMEM;
	}
	memset(devices, 0, nr_devs * sizeof(struct device));
	for (i = 0; i < nr_devs; i++) {
		if (iminor(devices[i]) == 0) {
			devices[i].bufferIN = buffer0;
			devices[i].bufferOut = buffer1;
		} else {		// (iminor(devices[i]) == 1)
			devices[i].bufferIN = buffer1;
			devices[i].bufferOut = buffer0;
		}
		mutex_init(&devices[i].mutex);
		setup_cdev(devices + i, i);		
	}

	printk(KERN_INFO "DM510: Hello from your device!\n");
	return 0;
}





/* Called when module is unloaded */
// or on failure
void dm510_cleanup_module( void ) {

	/* clean up code belongs here */

	int i;

	kfree(buffer0->start);
	kfree(buffer1->start);

	kfree(buffer0);
	kfree(buffer1);

	if (!devices)
		return; /* nothing else to release */

	for (i = 0; i < nr_devs; i++) {
		cdev_del(&devices[i].cdev);
	}
	kfree(devices);
	unregister_chrdev_region(first_devno, nr_devs);
	devices = NULL; /* pedantic */

	printk(KERN_INFO "DM510: Module unloaded.\n");
}







/* Called when a process tries to open the device file */
static int dm510_open( struct inode *inode, struct file *filp ) {
	
	/* device claiming code belongs here */

	struct device *dev;	

	// Identify which device is being opened and save a pointer to it for future use
	dev = container_of(inode->i_cdev, struct device, cdev);
	filp->private_data = dev;

	if (mutex_lock_interruptible(&dev->mutex))
		return -ERESTARTSYS;

	// Update the number of readers and writers 
	/* use f_mode, not  f_flags: it's cleaner (fs/open.c tells why) */
	if (filp->f_mode & FMODE_READ)
		dev->nreaders++;
	if (filp->f_mode & FMODE_WRITE)
		dev->nwriters++;
	mutex_unlock(&dev->mutex);

	// return nonseekable_open(inode, filp);		// Should this be used?
	return 0;
}









/* Called when a process closes the device file. */
static int dm510_release( struct inode *inode, struct file *filp ) {

	/* device release code belongs here */
		
	return 0;
}







/* Called when a process, which already opened the dev file, attempts to read from it. */
static ssize_t dm510_read( struct file *filp,
    char *buf,      /* The buffer to fill with data     */
    size_t count,   /* The max number of bytes to read  */
    loff_t *f_pos )  /* The offset in the file           */
{
	
	/* read code belongs here */
	
	return 0; //return number of bytes read
}







/* Called when a process writes to dev file */
static ssize_t dm510_write( struct file *filp,
    const char *buf,/* The buffer to get data from      */
    size_t count,   /* The max number of bytes to write */
    loff_t *f_pos )  /* The offset in the file           */
{

	/* write code belongs here */	
	
	return 0; //return number of bytes written
}







/* called by system call icotl */ 
long dm510_ioctl( 
    struct file *filp, 
    unsigned int cmd,   /* command passed from the user */
    unsigned long arg ) /* argument of the command */
{
	/* ioctl code belongs here */
	printk(KERN_INFO "DM510: ioctl called.\n");

	return 0; //has to be changed
}







module_init( dm510_init_module );
module_exit( dm510_cleanup_module );

MODULE_AUTHOR( "...Kasper Dam Mortensen, Diego Fernández Markus" );
MODULE_LICENSE( "GPL" );











// ================ DUMPSTER =================================

// I'm going to dump code here to edit as much as I want

// Functions that I am not sure we will need. 
static int scull_p_fasync(int fd, struct file *filp, int mode);
static int spacefree(struct scull_pipe *dev);








// ============ BRAINSTORM ========================
