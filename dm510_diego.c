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

	/* end of what really should have been in a .h file */



	// This is a buffer which is accessed by one device for reading and another for writing.
	struct buffer {
			wait_queue_head_t inq, outq;       /* read and write queues */
			char *start, *end;                 /* begin of buf, end of buf */		// I renamed it to start.
			int buffersize;                    /* used in pointer arithmetic */
			char *rp, *wp;                     /* where to read, where to write */
			int nreaders, nwriters;            /* number of openings for r/w */
			// struct fasync_struct *async_queue; /* asynchronous readers */
			struct mutex mutex;                /* mutual exclusion semaphore */
	};


	// This is a device which reads from one buffer and writes into another. 
	// This is initialised in the open method.
	struct dm510_device {
		struct cdev cdev;               /* Char device structure */		// Would this mean that buffer doesn't get a cdev? Probably
		struct mutex mutex;             /* mutual exclusion semaphore */		// I think this should be included.
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
	//module_param(nr_devs, int, S_IRUGO); makes a lot of errors
	module_param(nr_devs, int, 0);	/* FIXME check perms */
	module_param(buffer_size, int, 0);

	// These are our two devices.
	// This variable gets used in the init function.
	static struct dm510_device *devices;

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



	// A function to make double locking more elegant
	// Returns 0 if successful or the error number otherwise
	static int acquire_locks(struct dm510_device *dev, struct buffer *buf) {

		if (mutex_lock_interruptible(&dev->mutex))
			return -ERESTARTSYS;
		if (mutex_lock_interruptible(&buf->mutex)) {
			mutex_unlock(&dev->mutex);
			return -ERESTARTSYS;	
		}

		printk(KERN_INFO "DM510: Successfully acquired locks \n");
		return 0;
	}


	/* How much space is free? */
	static int spacefree(struct buffer *buf) {
		printk(KERN_INFO "DM510: sapcefree \n");
		if (buf->rp == buf->wp)
			return buf->buffersize - 1;
		return ((buf->rp + buf->buffersize - buf->wp) % buf->buffersize) - 1;
	}


	/* Wait for space for writing; caller must hold device semaphore.  On
	* error the semaphore will be released before returning. */
	static int getwritespace(struct buffer *buf, struct file *filp) {

		struct dm510_device *dev = filp->private_data; 
		int lock_check;

		while (spacefree(buf) == 0) { /* full */
			DEFINE_WAIT(wait);
			
			mutex_unlock(&dev->mutex);
			mutex_unlock(&buf->mutex);
			if (filp->f_flags & O_NONBLOCK)
				return -EAGAIN;
			//PDEBUG("\"%s\" writing: going to sleep\n",current->comm);  					do we need?
			prepare_to_wait(&buf->outq, &wait, TASK_INTERRUPTIBLE);
			if (spacefree(buf) == 0)
				schedule();
			finish_wait(&buf->outq, &wait);
			if (signal_pending(current))
				return -ERESTARTSYS; /* signal: tell the fs layer to handle it */

			// Reacquire the locks
			lock_check = acquire_locks(dev, bufOut);
			if (lock_check != 0) {
				return lock_check;
			}
		}
		printk(KERN_INFO "DM510: getwritespace \n");
		return 0;
	}	





	// Auxiliary function for initialising a single buffer.
	int setup_buffer(struct buffer **buf, int buffer_size) {
    	
		// Allocate the structure
    	*buf = kmalloc(sizeof(struct buffer), GFP_KERNEL);
    	if (!*buf) {
        	// Indicate error
			printk(KERN_INFO "DM510: setup buffer failed\n");
        	return -ENOMEM;
    	}
    	memset(*buf, 0, sizeof(struct buffer));

    	// Allocate the actual buffer
    	(*buf)->start = kmalloc(buffer_size, GFP_KERNEL);
    	if (!(*buf)->start) {
        	kfree(*buf); // Free allocated structure before returning
        	*buf = NULL;
			printk(KERN_INFO "DM510: setup buffer failed2 \n");
        	return -ENOMEM;
    	}

    	// Initialise stuff
    	init_waitqueue_head(&((*buf)->inq));
    	init_waitqueue_head(&((*buf)->outq));
    	mutex_init(&((*buf)->mutex));

    	// Initialise the offset pointers
    	(*buf)->buffersize = buffer_size;
    	(*buf)->end = (*buf)->start + (*buf)->buffersize;
    	(*buf)->rp = (*buf)->wp = (*buf)->start; /* rd and wr from the beginning */
		printk(KERN_INFO "DM510: setupbuffer succes \n");
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
			printk(KERN_NOTICE "Error %d adding device%d", err, index);
	}



	/* called when module is loaded */
	int dm510_init_module( void ) {

		int result, i;
		dev_t dev = 0;

		// First initialise both buffers.
		struct buffer *buffer0; // Declare a pointer to struct buffer
		struct buffer *buffer1; // Declare a pointer to struct buffer
		// Call setup_buffer function
		int result0 = setup_buffer(&buffer0, buffer_size);
		int result1 = setup_buffer(&buffer1, buffer_size);
		// Check if setup_buffer succeeded
		if (result0  != 0 || result1 != 0) {
    		printk(KERN_ERR "Failed to setup buffer: error code %d\n", result); // Should this be result?
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
			printk(KERN_WARNING "DM510: can't get major %d\n", major_number);
			return result;
		}

		// Allocates and initialises the devices
		first_devno = dev;
		devices = kmalloc(nr_devs * sizeof(struct dm510_device), GFP_KERNEL);
		if (!devices) {
			dm510_cleanup_module();
			return -ENOMEM;
		}
		memset(devices, 0, nr_devs * sizeof(struct dm510_device));
		for (i = 0; i < nr_devs; i++) {
			mutex_init(&devices[i].mutex);
			setup_cdev(devices + i, i);		
		}

		printk(KERN_INFO "DM510: Hello from your device!\n");
		return 0;
	}





	/* Called when a process tries to open the device file */
	static int dm510_open( struct inode *inode, struct file *filp ) {
	
		struct device *dev;	
		struct buffer *buf;
		int minor, lock_check;

		// Identify which device is being opened and save a pointer to it
		dev = container_of(inode->i_cdev, struct device, cdev);
		filp->private_data = dev;

		// Identify which buffer is being used and save a pointer to it
		minor = iminor(inode);

		if (minor == 0) {
			if (filp->f_mode & FMODE_READ) {
				buf = buffer0;
			} else if (filp->f_mode & FMODE_WRITE) {
				buf = buffer1;
			}
		} else if (minor == 1) {
			if (filp->f_mode & FMODE_READ) {
				buf = buffer1;
			} else if (filp->f_mode & FMODE_WRITE) {
				buf = buffer0;
			}
		}

		// Acquire the locks and update the number of readers and writers
		lock_check = acquire_locks(dev, buf);
		if (lock_check != 0) {
			return lock_check;
		}
		if (filp->f_mode & FMODE_READ) {
			buf->nreaders++;
		}
		if (filp->f_mode & FMODE_WRITE) {
			buf->nwriters++;
		}
		mutex_unlock(&dev->mutex);
		mutex_unlock(&buf->mutex);

		// return nonseekable_open(inode, filp);		// Should this be used? 
		return 0;
	}





	/* Called when a process closes the device file. */
	static int dm510_release( struct inode *inode, struct file *filp ) {
	
		struct device *dev = filp->private_data;
		struct buffer *buf;
		int minor, lock_check;
	
		/* remove this filp from the asynchronously notified filp's */
		// scull_p_fasync(-1, filp, 0);		// What is this?

		// MAYBE IMPORTANT NOTE:
		// The original function taken from pipe.c used mutex_lock instead of mutex_lock_interruptible.
		// I don't know why. I changed it to be able to use acquire_locks.
	
		// Identify which buffer is being used and save a pointer to it
		minor = iminor(inode);

		if (minor == 0) {
			if (filp->f_mode & FMODE_READ) {
				buf = buffer0;
			} else if (filp->f_mode & FMODE_WRITE) {
				buf = buffer1;
			}
		} else if (minor == 1) {
			if (filp->f_mode & FMODE_READ) {
				buf = buffer1;
			} else if (filp->f_mode & FMODE_WRITE) {
				buf = buffer0;
			}
		}

		// Acquire the locks and update the number of readers and writers
		lock_check = acquire_locks(dev, buf);
		if (lock_check != 0) {
			return lock_check;
		}
		if (filp->f_mode & FMODE_READ) {
			buf->nreaders--;
		}
		if (filp->f_mode & FMODE_WRITE) {
			buf->nwriters--;
		}
		mutex_unlock(&dev->mutex);
		mutex_unlock(&buf->mutex);
			
		return 0;
	}





	/* Called when a process, which already opened the dev file, attempts to read from it. */
	static ssize_t dm510_read( struct file *filp,
		char *buf,      /* The buffer to fill with data     */
		size_t count,   /* The max number of bytes to read  */
		loff_t *f_pos )  /* The offset in the file           */
	{

		struct dm510_device *dev = filp->private_data;
		struct buffer *bufIn;
		struct inode *inode;
		int minor, lock_check;

		// Identify which buffer is being used and save a pointer to it
		inode = file_inode(filp);
		minor = iminor(inode);

		if (minor == 0) {
			bufIn = buffer0;
		} else if (minor == 1) {
			bufIn = buffer1;
		}

		// Acquire the locks
		lock_check = acquire_locks(dev, bufIn);
		if (lock_check != 0) {
			return lock_check;
		}

		// Wait for data if there is nothing to read
		while (bufIn->rp == bufIn->wp) { /* nothing to read */
			mutex_unlock(&dev->mutex); /* release the locks */
			mutex_unlock(&bufIn->mutex);
			if (filp->f_flags & O_NONBLOCK){
				printk(KERN_INFO "DM510: read f \n");
				return -EAGAIN;
			}
			//PDEBUG("\"%s\" reading: going to sleep\n", current->comm);						do we need?
			if (wait_event_interruptible(bufIn->inq, (bufIn->rp != bufIn->wp))){
				printk(KERN_INFO "DM510: read f \n");
				return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
			}
			/* otherwise loop, but first reacquire the locks */
			lock_check = acquire_locks(dev, bufIn);
			if (lock_check != 0) {
				return lock_check;
			}
		}

		/* ok, data is there, return something */
		if (bufIn->wp > bufIn->rp)
			count = min(count, (size_t)(bufIn->wp - bufIn->rp));
		else /* the write pointer has wrapped, return data up to bufIn->end */
			count = min(count, (size_t)(bufIn->end - bufIn->rp));
		if (copy_to_user(buf, bufIn->rp, count)) {
			mutex_unlock (&dev->mutex);
			mutex_unlock (&bufIn->mutex);			
			printk(KERN_INFO "DM510: read f \n");
			return -EFAULT;
		}

		// Update rp to show how much has been read
		bufIn->rp += count;
		if (bufIn->rp == bufIn->end){}
			bufIn->rp = bufIn->start; /* wrapped */
		
		// Release the locks 
		mutex_unlock (&dev->mutex);
		mutex_unlock (&bufIn->mutex);

		/* finally, awake any writers and return */
		wake_up_interruptible(&bufIn->outq);
		//PDEBUG("\"%s\" did read %li bytes\n",current->comm, (long)count);
		printk(KERN_INFO "DM510: read s \n");
		return count;	
	}







	/* Called when a process writes to dev file */
	static ssize_t dm510_write( struct file *filp,
		const char *buf,/* The buffer to get data from      */
		size_t count,   /* The max number of bytes to write */
		loff_t *f_pos )  /* The offset in the file           */
	{

		struct dm510_device *dev = filp->private_data; 
		struct buffer *bufOut;
		struct inode *inode;
		int minor, result, lock_check;

		// Identify which buffer is being used and save a pointer to it
		inode = file_inode(filp);
		minor = iminor(inode);

		if (minor == 0) {
			bufOut = buffer1;
		} else if (minor == 1) {
			bufOut = buffer0;
		}

		// Acquire the locks
		lock_check = acquire_locks(dev, bufOut);
		if (lock_check != 0) {
			return lock_check;
		}

		/* Make sure there's space to write */
		result = getwritespace(bufOut, filp);
		if (result)				// ??
			return result; /* getwritespace called mutex_unlock(&bufOut->mutex) */
	
		/* ok, space is there, accept something */
		// Figure out how much
		count = min(count, (size_t)spacefree(bufOut));
		if (bufOut->wp >= bufOut->rp)
			count = min(count, (size_t)(bufOut->end - bufOut->wp)); /* to end-of-buf */
		else /* the write pointer has wrapped, fill up to rp-1 */
			count = min(count, (size_t)(bufOut->rp - bufOut->wp - 1));
		//PDEBUG("Going to accept %li bytes to %p from %p\n", (long)count, bufOut->wp, buf);			do we need?

		// Copy data into our buffer
		if (copy_from_user(bufOut->wp, buffer, count)) {
			mutex_unlock(&dev->mutex);
			mutex_unlock(&bufOut->mutex);
			return -EFAULT;
		}

		// Update wp to show that something was written
		bufOut->wp += count;
		if (bufOut->wp == bufOut->end)
			bufOut->wp = bufOut->start; /* wrapped */

		// Release the locks
		mutex_unlock(&dev->mutex);
		mutex_unlock(&bufOut->mutex);

		/* finally, awake any reader */
		wake_up_interruptible(&bufOut->inq);  /* blocked in read() and select() */

		/* and signal asynchronous readers, explained late in chapter 5 */
		//if (bufOut->async_queue)																do we need this? causes error
			//kill_fasync(&bufOut->async_queue, SIGIO, POLL_IN); 									
		//PDEBUG("\"%s\" did write %li bytes\n",current->comm, (long)count);					do we need?
		printk(KERN_INFO "DM510: write \n");
		return count;
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
	// code ideas:
	// setup_dev and setup_buffer should maybe be void

	// do we need pdebug???, if we do we need to include i think
	
	// Can PDEBUG be replaced by printk?

	// I am not 100% sure that double locking is done right. I did what made sense for me, but it
	// feels like one of those things that could easily be wrong. 

	// I have changed getwritespace so that it acquires and releases locks for dev and buf. I am confident 
	// that it is supposed to be like that.

	// Does getwritespace work? Is the filp supposed to represent the device and not the buffer? 

	// What about the asynchronous readers? What are they? I will read more on them at some point.

	// Several processes at a time must be able to open each device for reading, 
	// only one process at a time should be able to open a device for writing.
	// Does our program work this way? Do we need to check the intentions of a process before opening a device?





	// Functions that I am not sure we will need:

	//static int scull_p_fasync(int fd, struct file *filp, int mode);








	// ============ BRAINSTORM ========================


