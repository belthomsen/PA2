// read
/**
 * File:	lkmasg2.c
 * Adapted for Linux 5.15 by: John Aedo
 * Class:	COP4600-SP23
 
 * Group: Luis Hernandez, John Sierra, Isabel Thomsen
 */

/**
 * TODO:
 * Do devices/classes need to be renamed?
 * Double check correct externing of the array buffer
 * Do the mutex locks need to be externed too?
 * Are the source files linked correctly?
 * Make the makefile
 * Insert the printk statements for Condition and Output
 * Make sure waiting for the mutex works properly and also implement the wait_queue finally
 * Make sure buffer full and buffer empty conditions are accounted for 
 */
#include "lkmasg2_output.c"	  // Check if this should be omitted due to the special conditions
// need a header file?
#include <linux/init.h>       // Macros used to mark up functions e.g. __init __exit
#include <linux/module.h>	  // Core header for modules.
#include <linux/device.h>	  // Supports driver model.
#include <linux/kernel.h>	  // Kernel header for convenient functions.
#include <linux/fs.h>		  // File-system support.
#include <linux/uaccess.h>	  // User access copy function support.
#include <linux/sched/signal.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/slab.h>

#define OUTPUT_DEVICE_NAME "lkmasg2Output" // Device name.
#define OUTPUT_CLASS_NAME "output"	  ///< The device class -- this is a character device driver
#define BUFFER_SIZE 1024

MODULE_LICENSE("GPL");						 ///< The license type -- this affects available functionality
MODULE_AUTHOR("John Aedo");					 ///< The author -- visible when you use modinfo
MODULE_DESCRIPTION("lkmasg2 Output Kernel Module"); ///< The description -- see modinfo
MODULE_VERSION("0.1");						 ///< A version number to inform users

/**
 * Important variables that store data and keep track of relevant information.
 */
static int major_number;
// static char   message[256] = {0};           ///< Memory for the string that is passed from userspace
// maximum buffer message is 1024
extern char our_buffer[BUFFER_SIZE] = {0};
EXPORT_SYMBOL(our_buffer);					// Hopefully will make sure the buffer is shared?
static short  size_of_message;              ///< Used to remember the size of the string stored
static int    numberOpens = 0;              ///< Counts the number of times the device is opened

// Check back to make sure if the lkmasg2 class and device need to be reader-specific or not
static struct class *lkmasg2OutputClass = NULL;	///< The device-driver class struct pointer
static struct device *lkmasg2OutputDevice = NULL; ///< The device-driver device struct pointer

// example taken from Derek Malloy's ebbchar_mutex.c
// do the mutexes need to be externed also?
// double check formatting from the example or if it's just declared mutex
// static DEFINE_MUTEX(lkmasg2_mutex);
// EXPORT_SYMBOL(lkmasg2_mutex);
static wait_queue_head_t output_queue;

/**
 * Prototype functions for file operations.
 */
static int output_open(struct inode *, struct file *);
static int output_close(struct inode *, struct file *);
static ssize_t output_read(struct file *, char *, size_t, loff_t *);
static ssize_t output_write(struct file *, const char *, size_t, loff_t *);

static size_t output_size=0; //@@@@@
/**
 * File operations structure and the functions it points to.
 */
static struct file_operations fops =
	{
		.owner = THIS_MODULE,
		.open = output_open,
		.release = output_close,
		.read = output_read,
		.write = output_xwrite,
};

/**
 * Initializes module at installation
 */
int init_module(void)
{
	printk(KERN_INFO "lkmasg2 Output Device: initialize\n");
	 	// Allocate a major number for the device.

	printk(KERN_INFO "lkmasg2 Output Device: acquired lock");
	memset(our_buffer, 0, BUFFER_SIZE)
	// mutex_lock(&lkmasg2_mutex);

	major_number = register_chrdev(0, OUTPUT_DEVICE_NAME, &fops);
	if (major_number < 0)
	{
		printk(KERN_ALERT "lkmasg2 Output Device could not register number.\n");
		return major_number;
	}
	printk(KERN_INFO "lkmasg2 Output Device: registered correctly with major number %d\n", major_number);
	// Register the device class
	lkmasg2OutputClass = class_create(THIS_MODULE, OUTPUT_CLASS_NAME);
	if (IS_ERR(lkmasg2OutputClass))
	{ 
		// Check for error and clean up if there is
		unregister_chrdev(major_number, OUTPUT_DEVICE_NAME);
		printk(KERN_ALERT "Failed to register device class\n");
		return PTR_ERR(lkmasg2OutputClass); // Correct way to return an error on a pointer
	}
	printk(KERN_INFO "lkmasg2 Output Device: device class registered correctly\n");

	// Register the device driver
	lkmasg2OutputDevice = device_create(lkmasg2OutputClass, NULL, MKDEV(major_number, 0), NULL, OUTPUT_DEVICE_NAME);
	if (IS_ERR(lkmasg2OutputDevice))
	{								 // Clean up if there is an error
		class_destroy(lkmasg2OutputClass); // Repeated code but the alternative is goto statements
		unregister_chrdev(major_number, OUTPUT_DEVICE_NAME);
		printk(KERN_ALERT "lkmasg2 Output: Failed to create the device\n");
		return PTR_ERR(lkmasg2OutputDevice);
	}
	printk(KERN_INFO "lkmasg2 Output: Releasing the lock");
	// mutex_unlock(&lkmasg2_mutex);
	// mutex_init(&lkmasg2_mutex);
	init_waitqueue_head(&output_queue);
	printk(KERN_INFO "lkmasg2 Output Device: device class created correctly\n"); // Made it! device was initialized

	return 0;
}

/*
 * Removes module, sends appropriate message to kernel
 */
void cleanup_module(void)
{
	// mutex_destroy(&lkmasg2_mutex);
	printk(KERN_INFO "lkmasg2 Output Device: removing module.\n");
	device_destroy(lkmasg2OutputClass, MKDEV(major_number, 0)); // remove the device
	class_unregister(lkmasg2OutputClass);						  // unregister the device class
	class_destroy(lkmasg2OutputClass);						  // remove the device class
	unregister_chrdev(major_number, OUTPUT_DEVICE_NAME);		  // unregister the major number
	printk(KERN_INFO "lkmasg2 Output Device: Goodbye from the LKM!\n");
	unregister_chrdev(major_number, OUTPUT_DEVICE_NAME);
	return;
}

/*
 * Opens device module, sends appropriate message to kernel
 */
static int output_open(struct inode *inodep, struct file *filep)
{
	// try the lock first
	// if(!mutex_trylock(lkmasg2_mutex)) {
	// 	printk(KERN_ALERT "lkmasg2 Input Device: Device in use by another process");
	// 	return -EBUSY;
	// }

	numberOpens++;
	printk(KERN_INFO "lkmasg2 Output Device: device opened %d time(s)\n", numberOpens);
	return 0;
}
static int output_close(struct inode *inodep, struct file *filep){
   // mutex_unlock(&lkmasg2_mutex);                      // release the mutex (i.e., lock goes up)
   printk(KERN_INFO "lkmasg2 Output Device: Device successfully closed\n");
   return 0;
}

/*
 * Reads from device, displays in userspace, and deletes the read data
 */
static ssize_t output_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
	int buffer_len = strlen(our_buffer);
	ssize_t leftover = min(buffer_len - *offset, len);
	printk(KERN_INFO "lkmasg2 Output Device: Entered read");
	if (leftover <= 0) {
		return 0;
	}

	int error_count = 0;
	error_count = copy_to_user(buffer, our_buffer + *offset, leftover);
	
	if (error_count == 0) {
		return (size_of_message=0);  // clear the position to the start and return 0
	} else {
      		// printk(KERN_INFO "lkmasg2: Failed to send %d characters to the user\n", error_count);
      		return -EFAULT;              // Failed -- return a bad address message (i.e. -14)
	}

	// insert truncation condition 
	printk(KERN_INFO "read stub");
	*offset += leftover;
	return leftover;
}

/*
 * Writes to the device
 */
static ssize_t output_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
	size_t message;
	size_t progress = 0;

	

	printk(KERN_INFO "lkmasg2 Output: entered write()");
	
	if(len==0){
		printk(KERN_WARNING "Output buffer is empty");
		return -ENOSPC; //idk what this is
	}

	if(len > output_size){
		len=output_size;
	}
	
	printk(KERN_INFO "lkmasg2 Writer: acquired the lock");
	mutex_lock(our_buffer);
	
	if(copy_from_user(our_buffer, buffer, len)!=0){
		progress= -EFAULT;
		printk(KERN_WARNING "Output failed to write");
		goto jump; 
	}
	
 printk(KERN_INFO "Output in critical section");	
	
	progress=len;
	
	jump:
		 printk(KERN_INFO "Output released lock");
		 return len;
	return progress;
}

//module_init(init_module);
//module_exit(cleanup_module);
