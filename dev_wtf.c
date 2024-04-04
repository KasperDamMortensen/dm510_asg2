
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

#define DEVICE_NAME "dm510_dev" /* Dev name as it appears in /proc/devices */
#define MAJOR_NUMBER 255
#define MIN_MINOR_NUMBER 0
#define MAX_MINOR_NUMBER 1

#define DEVICE_COUNT 2
#define BUFFER_SIZE 10	// This number should probably be changed later
#define MAX_READERS 10  // This number should probably be changed later


#include "dm510.h"

// This is a buffer which is accessed by one device for reading and another for writing.
struct buffer {
        wait_queue_head_t inq, outq;       /* read and write queues */
        char *start, *end;                 /* begin of buf, end of buf */		// I renamed it to start.
        int buffersize;                    /* used in pointer arithmetic */
        char *rp, *wp;                     /* where to read, where to write */
        int nreaders, nwriters;            /* number of openings for r/w */
        int maxReaders;                     /* max number of readers at a time */
        struct mutex mutex;                /* mutual exclusion semaphore */
};


// This is a device which reads from one buffer and writes into another. 
struct dm510_device {
    struct buffer *bufferIn;			// The buffer it reads from.
    struct buffer *bufferOut;		// The buffer it writes to.
    struct cdev cdev;               /* Char device structure */		
    // struct mutex mutex;             /* mutual exclusion semaphore */		// I think this should be removed.
};



/* 
* Our parameters which can be set at load time.
*/

int major_number =   MAJOR_NUMBER;		
int minor_number =   0;					
int nr_devs = DEVICE_COUNT;				// number of devices 		// static? I saw this somewhere...
int buffer_size =  BUFFER_SIZE;			 
int max_readres = MAX_READERS;          
dev_t first_devno;						// Our first device number 

// I suppose that module_param allows you to modify some structure safely and easily...
module_param(major_number, int, S_IRUGO);
module_param(minor_number, int, S_IRUGO);
//module_param(nr_devs, int, S_IRUGO); makes a lot of errors
module_param(nr_devs, int, 0);	/* FIXME check perms */
module_param(buffer_size, int, 0);

// These are our two devices.
// This variable gets used in the init function.
static struct dm510_device *devices;

// These are our two buffers.
static struct buffer *buffer0 = NULL;
static struct buffer *buffer1 = NULL;

/* file operations struct */
static struct file_operations dm510_fops = {
    .owner   = THIS_MODULE,
    .read    = dm510_read,
    .write   = dm510_write,
    .open    = dm510_open,
    .release = dm510_release,
    .unlocked_ioctl   = dm510_ioctl
};


/* How much space is free? */
static int spacefree(struct buffer *buf) {
    //printk(KERN_INFO "DM510: spacefree \n");
    if (buf->rp == buf->wp)
        return buf->buffersize - 1;
    return ((buf->rp + buf->buffersize - buf->wp) % buf->buffersize) - 1;
}


/* Wait for space for writing; caller must hold device semaphore.  On
* error the semaphore will be released before returning. */
static int getwritespace(struct buffer *buf, struct file *filp)
{
    while (spacefree(buf) == 0) { /* full */
        //printk(KERN_NOTICE "getwritespace: space left %d, minor number:%d\n", spacefree(buf), iminor(filp->f_inode));
        DEFINE_WAIT(wait);
        
        mutex_unlock(&buf->mutex);
        if (filp->f_flags & O_NONBLOCK)
            return -EAGAIN;
        PDEBUG("\"%s\" writing: going to sleep:%d\n",current->comm,iminor(filp->f_inode));		//do we need?
        prepare_to_wait(&buf->outq, &wait, TASK_INTERRUPTIBLE);
        if (spacefree(buf) == 0)
            schedule();
        finish_wait(&buf->outq, &wait);
        if (signal_pending(current))
            return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
        if (mutex_lock_interruptible(&buf->mutex))
            return -ERESTARTSYS;
    }
    //printk(KERN_INFO "DM510: getwritespace \n");
    return 0;
}	





// Auxiliary function for initialising a single buffer.
int setup_buffer(struct buffer **buf, int buffer_size) {
    
    // Allocate the structure
    *buf = kmalloc(sizeof(struct buffer), GFP_KERNEL);
    if (!*buf) {
        // Indicate error
        //printk(KERN_INFO "DM510: setup buffer: failed\n");
        return -ENOMEM;
    }
    memset(*buf, 0, sizeof(struct buffer));

    // Allocate the actual buffer
    (*buf)->start = kmalloc(buffer_size, GFP_KERNEL);
    if (!(*buf)->start) {
        kfree(*buf); // Free allocated structure before returning
        *buf = NULL;
        //printk(KERN_INFO "DM510: setup buffer failed2 \n");
        return -ENOMEM;
    }

    // Initialise stuff
    init_waitqueue_head(&((*buf)->inq));
    init_waitqueue_head(&((*buf)->outq));
    mutex_init(&((*buf)->mutex));

    // Initialise the offset pointers
    (*buf)->buffersize = buffer_size;
    (*buf)->maxReaders = max_readres;
    (*buf)->end = (*buf)->start + (*buf)->buffersize;
    (*buf)->rp = (*buf)->wp = (*buf)->start; /* rd and wr from the beginning */
    //printk(KERN_INFO "DM510: setupbuffer succes \n");

    return 0; // Success
}


/* Called when module is unloaded */
// or on failure
void dm510_cleanup_module( void ) {

    int i;

    kfree(buffer0->start);
    kfree(buffer1->start);

    kfree(buffer0);
    kfree(buffer1);

    if (!devices){
        return; /* nothing else to release */
    }
    for (i = 0; i < nr_devs; i++) {
        cdev_del(&devices[i].cdev);
    }
    kfree(devices);
    unregister_chrdev_region(first_devno, nr_devs);
    devices = NULL; /* pedantic */

    printk(KERN_INFO "DM510: Module unloaded.\n");
}

// Auxiliary function to dm510_init_module
/*
* Set up a cdev entry.
*/
static void setup_cdev(struct dm510_device *dev, int index) {
    int err, devno = first_devno + index;
    
    cdev_init(&dev->cdev, &dm510_fops);
    dev->cdev.owner = THIS_MODULE;
    err = cdev_add (&dev->cdev, devno, 1);
    printk(KERN_INFO "DM510: setup cdev \n");
    /* Fail gracefully if need be */
    if (err)
        printk(KERN_NOTICE "Error %d adding buffer%d", err, index);
}



/* called when module is loaded */
int dm510_init_module( void ) {

    int result, i;
    dev_t dev = 0;

    // Initialise the buffers
    if (buffer0 == NULL || buffer1 ==NULL){
        int result0 = setup_buffer(&buffer0, buffer_size);
        int result1 = setup_buffer(&buffer1, buffer_size);
        // Check if setup_buffer succeeded
        if (result0  != 0 || result1 != 0) {
            return -1;
        }
    }

    // This part makes a dev_t, which is a number containing both a major and a minor number. 
    // And it then registers them as belonging to a device. If the chosen major is available, it is used.
    // Otherwise a new one gets assigned by the kernel. 
    if (major_number) {
        dev = MKDEV(major_number, minor_number);
        result = register_chrdev_region(dev, nr_devs, "DM510"); 
    } else {
        result = alloc_chrdev_region(&dev, minor_number, nr_devs,
                "DM510");
        major_number = MAJOR(dev);
    }
    if (result < 0) {
        //printk(KERN_WARNING "DM510: can't get major %d\n", major_number);
        return result;
    }

    // Allocate and initialise the devices
    first_devno = dev;
    devices = kmalloc(nr_devs * sizeof(struct dm510_device), GFP_KERNEL);
    if (!devices) {
        dm510_cleanup_module();
        return -ENOMEM;
    }
    memset(devices, 0, nr_devs * sizeof(struct dm510_device));
    for (i = 0; i < nr_devs; i++) {
        setup_cdev(devices + i, i);		

        if (i == 0) {
            devices[i].bufferIn = buffer0;
            devices[i].bufferOut = buffer1;
            //printk(KERN_INFO "DM510: device zero given buffer\n");

        } else {
            devices[i].bufferIn = buffer1;
            devices[i].bufferOut = buffer0;
            //printk(KERN_INFO "DM510: device one given buffers\n");
            }
    }

    printk(KERN_INFO "DM510: Hello from your device!\n");
    return 0;
}






/* Called when a process tries to open the device file */
static int dm510_open( struct inode *inode, struct file *filp ) {
    
    struct dm510_device *dev;	

    // Identify which device is being opened and save a pointer to it for future use
    dev = container_of(inode->i_cdev, struct dm510_device, cdev);
    filp->private_data = dev;

    // Check the intentions of the device and acquire locks and update readers and writers accordingly
    if (filp->f_mode & FMODE_READ) {
        if (mutex_lock_interruptible(&dev->bufferIn->mutex))
            return -ERESTARTSYS;
        if ((dev->bufferIn->nreaders) >= (dev->bufferIn->maxReaders)) {
            mutex_unlock(&dev->bufferIn->mutex);
            return -EBUSY;
        }
        dev->bufferIn->nreaders++;	
        mutex_unlock(&dev->bufferIn->mutex);	
    }
    if (filp->f_mode & FMODE_WRITE) {
        if (mutex_lock_interruptible(&dev->bufferOut->mutex))
            return -ERESTARTSYS;
        if ((dev->bufferOut->nwriters) >= 1) {
            mutex_unlock(&dev->bufferOut->mutex);
            return -EBUSY;
        }
        dev->bufferOut->nwriters++;	
        mutex_unlock(&dev->bufferOut->mutex);
    }

    printk(KERN_INFO "DM510: open \n");
    return 0;
}






/* Called when a process closes the device file. */
static int dm510_release( struct inode *inode, struct file *filp ) {

    struct dm510_device *dev = filp->private_data;

    // Check the intentions of the device and acquire locks and update readers and writers accordingly
    if (filp->f_mode & FMODE_READ) {
        if (mutex_lock_interruptible(&dev->bufferIn->mutex))
            return -ERESTARTSYS;
        
        dev->bufferIn->nreaders--;	
        mutex_unlock(&dev->bufferIn->mutex);	
    }
    if (filp->f_mode & FMODE_WRITE) {
        if (mutex_lock_interruptible(&dev->bufferOut->mutex))
            return -ERESTARTSYS;
    
        dev->bufferOut->nwriters--;	
        mutex_unlock(&dev->bufferOut->mutex);
    }

    printk(KERN_INFO "DM510: release \n");
    return 0;
}







/* Called when a process, which already opened the dev file, attempts to read from it. */
static ssize_t dm510_read( struct file *filp,
    char *buf,      /* The buffer to fill with data     */
    size_t count,   /* The max number of bytes to read  */
    loff_t *f_pos )  /* The offset in the file           */
{

    struct dm510_device *dev = filp->private_data;
    struct buffer *bufIn = dev->bufferIn;

    if (mutex_lock_interruptible(&bufIn->mutex)){	
        //printk(KERN_INFO "DM510: read f1 \n");
        return -ERESTARTSYS;
    }

    // Wait for data if there is nothing to read
    while (bufIn->rp == bufIn->wp) { /* nothing to read */
        mutex_unlock(&bufIn->mutex); /* release the lock */
        if (filp->f_flags & O_NONBLOCK){
            //printk(KERN_INFO "DM510: read f2 \n");
            return -EAGAIN;
        }
        if (wait_event_interruptible(bufIn->inq, (bufIn->rp != bufIn->wp))){
            //printk(KERN_INFO "DM510: read f3 \n");
            return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
        }
        /* otherwise loop, but first reacquire the lock */
        if (mutex_lock_interruptible(&bufIn->mutex)){
            //printk(KERN_INFO "DM510: read f4 \n");
            return -ERESTARTSYS;
        }
    }
    
    /* ok, data is there, return something */
    if (bufIn->wp > bufIn->rp)
        count = min(count, (size_t)(bufIn->wp - bufIn->rp));
    else /* the write pointer has wrapped, return data up to dev->end */
        count = min(count, (size_t)(bufIn->end - bufIn->rp));
    if (copy_to_user(buf, bufIn->rp, count)) {
        mutex_unlock (&bufIn->mutex);
        //printk(KERN_INFO "DM510: read f \n");
        return -EFAULT;
    }

    // Update rp to show how much has been read
    dev->bufferIn->rp += count; 
    if (bufIn->rp == bufIn->end)
        bufIn->rp = bufIn->start; /* wrapped */

    mutex_unlock (&bufIn->mutex);

    /* finally, awake any writers and return */
    wake_up_interruptible(&bufIn->outq);

    //printk(KERN_INFO "DM510: read s \n");
    return count;	
}







/* Called when a process writes to dev file */
static ssize_t dm510_write( struct file *filp,
    const char *buffer,/* The buffer to get data from      */
    size_t count,   /* The max number of bytes to write */
    loff_t *f_pos )  /* The offset in the file           */
{

    struct buffer *bufOut = ((struct dm510_device *)filp->private_data)->bufferOut;
    int result;

    if (mutex_lock_interruptible(&bufOut->mutex))
        return -ERESTARTSYS;

    /* Make sure there's space to write */
    result = getwritespace(bufOut, filp);
    if (result <=0)	{	
		mutex_unlock(&bufOut->mutex);		
        return result; /* getwritespace called mutex_unlock(&bufOut->mutex) */
	}
    /* ok, space is there, accept something */
    // Figure out how much
    count = min(count, (size_t)spacefree(bufOut));
    if (bufOut->wp >= bufOut->rp)
        count = min(count, (size_t)(bufOut->end - bufOut->wp)); /* to end-of-buf */
    else /* the write pointer has wrapped, fill up to rp-1 */
        count = min(count, (size_t)(bufOut->rp - bufOut->wp - 1));

    // Copy data into our buffer
    if (copy_from_user(bufOut->wp, buffer, count)) {
        mutex_unlock (&bufOut->mutex);
        return -EFAULT;
    }

    // Update wp to show that something was written
    bufOut->wp += count;
    if (bufOut->wp == bufOut->end)
        bufOut->wp = bufOut->start; /* wrapped */

    mutex_unlock(&bufOut->mutex);

    /* finally, awake any reader */
    wake_up_interruptible(&bufOut->inq);  
								
    //printk(KERN_INFO "DM510: write \n");
    return count;
}





/* called by system call icotl */ 
// Works on the bufferIn of the device given as the first argument.
long dm510_ioctl( 
    struct file *filp, 
    unsigned int cmd,   /* command passed from the user */
    unsigned long arg ) /* argument of the command */
{

    struct dm510_device *dev = filp->private_data;
    struct buffer *bufIn = dev->bufferIn;
    int retval = 0, err = 0;
    int data;
    char *new_start;

    /* ioctl code belongs here */
    printk(KERN_INFO "DM510: ioctl called.\n");

    /*
    * extract the type and number bitfields, and don't decode
    * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
    */
    if (_IOC_TYPE(cmd) != DM510_IOC_MAGIC) return -ENOTTY;
    if (_IOC_NR(cmd) > DM510_IOC_MAXNR) return -ENOTTY;

    /*
    * the direction is a bitmask, and VERIFY_WRITE catches R/W
    * transfers. `Type' is user-oriented, while
    * access_ok is kernel-oriented, so the concept of "read" and
    * "write" is reversed
    */
   

    switch(cmd) {

        // Adjust the size of the bufIn of the device given
        // Must be at least large enough for the data still to be read
        case DM510_IOC_ADJUST_BUFFERSIZE:

            // Check how much data is written inside the current buffer.
            if (bufIn->rp == bufIn->wp) {
                data = 0;
            } else if (bufIn->rp < bufIn->wp) {
                data = (bufIn->wp - bufIn->rp);
            } else {     // rp > wp
                data = ((bufIn->end - bufIn->rp) + (bufIn->wp - bufIn->start));
            }

            // Check that the buffer size isn't smaller than the data inside.
            if (data > arg) {
                return -EINVAL;
            }

            // Allocate memory with the desired size.
            new_start = kmalloc(arg, GFP_KERNEL);
            if (!new_start) {
                return -ENOMEM;
            }

            // Copy the data into the new buffer.
            if (data != 0) {
                if (bufIn->rp < bufIn->wp) {
                    memcpy(new_start, bufIn->rp, data);
                } else {        // (rp > wp)
                    memcpy(new_start, bufIn->rp, (bufIn->end - bufIn->rp));
                    memcpy((new_start + (bufIn->end - bufIn->rp)), bufIn->start, (bufIn->wp - bufIn->start));
                }
            }

            // Adjust all the pointers
            bufIn->start = new_start;
            bufIn->buffersize = arg;
            bufIn->rp = bufIn->start;
            if (bufIn->rp < bufIn->wp) {
                bufIn->wp = bufIn->start + data;
            } else {
                bufIn->wp = bufIn->start+((bufIn->end - bufIn->rp) + (bufIn->wp - bufIn->start));
            }

            return 0;
                
            
        case DM510_IOC_ADJUST_NREADERS: 

				if (dev->bufferIn->nreaders <= arg){
					dev->bufferIn->maxReaders = arg;
					return 0;
				}else{
					return -EINVAL;
				} 
            

        default:  /* redundant, as cmd was checked against MAXNR */
        return -ENOTTY;
    }

    return retval; //has to be changed
	
		
}








module_init( dm510_init_module );
module_exit( dm510_cleanup_module );

MODULE_AUTHOR( "...Kasper Dam Mortensen, Diego Fernández Markus" );
MODULE_LICENSE( "GPL" );











// ================ DUMPSTER =================================
// code ideas:
//setup_dev and setup_buffer should maybe be void
//in init we need to make an inode, because iminor() only works on that type
// do we  need pdebug???, if we do we need to include i think








// ============ BRAINSTORM ========================


/* =========== VERSION ==================

This version includes DM510_IOC_ADJUST_BUFFERSIZE and passes the moduletest.
It has also been modified to use the header file dm510.h.
It now allows multiple readers to open a device at a time, with a limit saved as a field of the structure buffer. 
I have also cleaned it up a bit.

*/
