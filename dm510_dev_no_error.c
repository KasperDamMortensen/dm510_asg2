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
		struct buffer *bufferIn;			// The buffer it reads from.
		struct buffer *bufferOut;		// The buffer it writes to.
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
		//printk(KERN_INFO "DM510: sapcefree \n");
		if (buf->rp == buf->wp)
			return buf->buffersize - 1;
		return ((buf->rp + buf->buffersize - buf->wp) % buf->buffersize) - 1;
	}


	/* Wait for space for writing; caller must hold device semaphore.  On
	* error the semaphore will be released before returning. */
	static int getwritespace(struct buffer *buf, struct file *filp)
	{
		while (spacefree(buf) == 0) { /* full */
			DEFINE_WAIT(wait);
			
			mutex_unlock(&buf->mutex);
			if (filp->f_flags & O_NONBLOCK)
				return -EAGAIN;
			PDEBUG("\"%s\" writing: going to sleep:%d\n",current->comm,iminor(filp->f_inode));  					//do we need?
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
			printk(KERN_INFO "DM510: setup buffer: failed\n");
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
			printk(KERN_NOTICE "Error %d adding buffer%d", err, index);
	}



	/* called when module is loaded */
	int dm510_init_module( void ) {

		// Here starts the code taken from scull_init_module.
		int result, i;
		dev_t dev = 0;

		// Call setup_buffer function
		if (buffer0 == NULL || buffer1 ==NULL){
			int result0 = setup_buffer(&buffer0, buffer_size);
			int result1 = setup_buffer(&buffer1, buffer_size);
			// Check if setup_buffer succeeded
			if (result0  != 0 || result1 != 0) {
    			return -1;
			}
		}
		
	
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
		devices = kmalloc(nr_devs * sizeof(struct dm510_device), GFP_KERNEL);
		if (!devices) {
			dm510_cleanup_module();
			return -ENOMEM;
		}
		memset(devices, 0, nr_devs * sizeof(struct dm510_device));
		for (i = 0; i < nr_devs; i++) {
			mutex_init(&devices[i].mutex);
			setup_cdev(devices + i, i);		

			if (i == 0) {
            	devices[i].bufferIn = buffer0;
            	devices[i].bufferOut = buffer1;
        	} else {
            	devices[i].bufferIn = buffer1;
            	devices[i].bufferOut = buffer0;
        		}
		}
		
		

		printk(KERN_INFO "DM510: Hello from your device!\n");
		return 0;
	}





	







	/* Called when a process tries to open the device file */
	static int dm510_open( struct inode *inode, struct file *filp ) {
		
		struct dm510_device *dev;	
		int minor_number = iminor(inode);
		
    	printk(KERN_INFO "DM510: Device opened with minor number %d\n", minor_number);

		// Identify which device is being opened and save a pointer to it for future use

		// First initialise both buffers.

		
		dev = container_of(inode->i_cdev, struct dm510_device, cdev);
		filp->private_data = dev;


		if (mutex_lock_interruptible(&dev->mutex))
			return -ERESTARTSYS;

		// Update the number of readers and writers 
		/* use f_mode, not  f_flags: it's cleaner (fs/open.c tells why) */
		if (filp->f_mode & FMODE_READ)
			dev->bufferIn->nreaders++;		// Here it gets interesting...
		if (filp->f_mode & FMODE_WRITE)		// Does it need the buffer's lock 
			dev->bufferOut->nwriters++;		// to check these values?
		mutex_unlock(&dev->mutex);

		// return nonseekable_open(inode, filp);		// Should this be used? 
		//printk(KERN_INFO "DM510: open \n");
		return 0;
	}









	/* Called when a process closes the device file. */
	static int dm510_release( struct inode *inode, struct file *filp ) {

		struct dm510_device *dev = filp->private_data;

		/* remove this filp from the asynchronously notified filp's */
		// scull_p_fasync(-1, filp, 0);		// What is this?

		mutex_lock(&dev->mutex);			// Not interruptible? No if statement or -ERESTARTSYS either...
		if (filp->f_mode & FMODE_READ)
			dev->bufferIn->nreaders--;
		if (filp->f_mode & FMODE_WRITE)		// Not an else if?
			dev->bufferOut->nwriters--;
		mutex_unlock(&dev->mutex);
		//if (dev->bufferIn->nreaders + dev->bufferInnwriters == 0) { //skal ændres så den fjerner og checker device i stedet for buffer
			//kfree(dev->buffer);
			//dev->buffer = NULL; /* the other fields are not checked on open */
		//}
		printk(KERN_INFO "DM510: release \n");
		return 0;
	}







	/* Called when a process, which already opened the dev file, attempts to read from it. */
	static ssize_t dm510_read( struct file *filp,
		char *buf,      /* The buffer to fill with data     */
		size_t count,   /* The max number of bytes to read  */
		loff_t *f_pos )  /* The offset in the file           */
	{
		// This function isn't finished! The locks are all messed up, we need for both dev and bufIn.
		
		struct dm510_device *dev = filp->private_data;
		struct buffer *bufIn = dev->bufferIn;
		int minor_number = iminor(filp->f_inode);
   		printk(KERN_INFO "DM510: Device read from with minor number %d\n", minor_number);

		if (mutex_lock_interruptible(&bufIn->mutex)){	// We also need to acquire the buffer's lock.
			printk(KERN_INFO "DM510: read f1 \n");
			return -ERESTARTSYS;
		}
		while (bufIn->rp == bufIn->wp) { /* nothing to read */
			mutex_unlock(&bufIn->mutex); /* release the lock */
			if (filp->f_flags & O_NONBLOCK){
				printk(KERN_INFO "DM510: read f2 \n");
				return -EAGAIN;
			}
			PDEBUG("\"%s\" reading: going to sleep\n", current->comm);						//do we need?
			if (wait_event_interruptible(bufIn->inq, (bufIn->rp != bufIn->wp))){
				printk(KERN_INFO "DM510: read f3 \n");
				return -ERESTARTSYS; /* signal: tell the fs layer to handle it */
			}
			/* otherwise loop, but first reacquire the lock */
			if (mutex_lock_interruptible(&bufIn->mutex)){
				printk(KERN_INFO "DM510: read f4 \n");
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
			printk(KERN_INFO "DM510: read f \n");
			return -EFAULT;
		}
		dev->bufferIn->rp += count;// valgte buffer tilfældigt for at fjerne en fejl, ved ikke om det skal være In eller Out.
		if (bufIn->rp == bufIn->end){}
			bufIn->rp = bufIn->start; /* wrapped */
		mutex_unlock (&bufIn->mutex);

		/* finally, awake any writers and return */
		wake_up_interruptible(&bufIn->outq);
		PDEBUG("\"%s\" did read %li bytes\n",current->comm, (long)count);
		//printk(KERN_INFO "DM510: read s \n");
		return count;	
	}







	/* Called when a process writes to dev file */
	static ssize_t dm510_write( struct file *filp,
		const char *buffer,/* The buffer to get data from      */
		size_t count,   /* The max number of bytes to write */
		loff_t *f_pos )  /* The offset in the file           */
	{

		/* write code belongs here */	

		struct buffer *bufOut = ((struct dm510_device *)filp->private_data)->bufferOut;
		int result;
		int minor_number = iminor(filp->f_inode);
    	printk(KERN_INFO "DM510: Device write from with minor number %d\n", minor_number);

		if (mutex_lock_interruptible(&bufOut->mutex))
			return -ERESTARTSYS;

		/* Make sure there's space to write */
		result = getwritespace(bufOut, filp);
		if (result)				// ??
			return result; /* scull_getwritespace called mutex_unlock(&dev->mutex) */
	
		/* ok, space is there, accept something */
		count = min(count, (size_t)spacefree(bufOut));
		if (bufOut->wp >= bufOut->rp)
			count = min(count, (size_t)(bufOut->end - bufOut->wp)); /* to end-of-buf */
		else /* the write pointer has wrapped, fill up to rp-1 */
			count = min(count, (size_t)(bufOut->rp - bufOut->wp - 1));
		PDEBUG("Going to accept %li bytes to %p from %p\n", (long)count, bufOut->wp, bufOut);			//do we need?
		if (copy_from_user(bufOut->wp, buffer, count)) {
			mutex_unlock (&bufOut->mutex);
			return -EFAULT;
		}
		bufOut->wp += count;
		if (bufOut->wp == bufOut->end)
			bufOut->wp = bufOut->start; /* wrapped */
		mutex_unlock(&bufOut->mutex);

		/* finally, awake any reader */
		wake_up_interruptible(&bufOut->inq);  /* blocked in read() and select() */

		/* and signal asynchronous readers, explained late in chapter 5 */
		//if (bufOut->async_queue)																do we need this? causes error
			//kill_fasync(&bufOut->async_queue, SIGIO, POLL_IN); 									
		PDEBUG("\"%s\" did write %li bytes\n",current->comm, (long)count);					//do we need?
		//printk(KERN_INFO "DM510: write \n");
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
	//setup_dev and setup_buffer should maybe be void
	//in init we need to make an inode, because iminor() only works on that type
	// do we  need pdebug???, if we do we need to include i think


	// I'm going to dump code here to edit as much as I want

	// Functions that I am not sure we will need. 

	//static int scull_p_fasync(int fd, struct file *filp, int mode);
	//static int spacefree(struct scull_pipe *dev);








	// ============ BRAINSTORM ========================
