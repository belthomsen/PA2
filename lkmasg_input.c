#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/sched/signal.h>
#include "shared.h"

#define INPUT_DEVICE_NAME "lkmasg_input"
#define INPUT_CLASS_NAME "input_class"
#define SHARED_BUFFER_SIZE 1024

MODULE_LICENSE("GPL");

static int inputMajorNumber;
static struct class* inputClass = NULL;
static struct device* inputDevice = NULL;
char* sharedBuffer = NULL;
static struct mutex sharedBufferMutex;
static wait_queue_head_t inputQueue;

static ssize_t input_write(struct file* file, const char* buffer, size_t length, loff_t* offset);
static int input_open(struct inode *inodep, struct file *filep);
static int input_close(struct inode *inodep, struct file *filep);

static struct file_operations input_fops = {
    .owner = THIS_MODULE,
    .open = input_open,
    .release = input_close,
    .write = input_write,
};

static int __init input_init(void) {
    printk(KERN_INFO "Waiting on the lock\n");

    inputMajorNumber = register_chrdev(0, INPUT_DEVICE_NAME, &input_fops);
    if (inputMajorNumber < 0) {
        printk(KERN_ALERT "Failed to register input device driver with major number %d\n", inputMajorNumber);
        return inputMajorNumber;
    }

    inputClass = class_create(THIS_MODULE, INPUT_CLASS_NAME);
    if (IS_ERR(inputClass)) {
        unregister_chrdev(inputMajorNumber, INPUT_DEVICE_NAME);
        printk(KERN_ALERT "Failed to create input device class\n");
        return PTR_ERR(inputClass);
    }

    inputDevice = device_create(inputClass, NULL, MKDEV(inputMajorNumber, 0), NULL, INPUT_DEVICE_NAME);
    if (IS_ERR(inputDevice)) {
        class_destroy(inputClass);
        unregister_chrdev(inputMajorNumber, INPUT_DEVICE_NAME);
        printk(KERN_ALERT "Failed to create input device\n");
        return PTR_ERR(inputDevice);
    }



    sharedBuffer = kmalloc(SHARED_BUFFER_SIZE, GFP_KERNEL);
    if (!sharedBuffer) {
        device_destroy(inputClass, MKDEV(inputMajorNumber, 0));
        class_destroy(inputClass);
        unregister_chrdev(inputMajorNumber, INPUT_DEVICE_NAME);
        printk(KERN_ALERT "Failed to allocate shared buffer\n");
        return -ENOMEM;
    }
    mutex_init(&sharedBufferMutex);
    init_waitqueue_head(&inputQueue);

    printk(KERN_INFO "Input device driver registered with major number %d\n", inputMajorNumber);

    return 0;
}



/*
 * Removes module, sends appropriate message to kernel
 */
void cleanup_module(void)
{
    printk(KERN_INFO "Input device driver is being removed.\n");
    mutex_destroy(&sharedBufferMutex);
    device_destroy(inputClass, MKDEV(inputMajorNumber, 0));
    class_unregister(inputClass);
    class_destroy(inputClass);
    unregister_chrdev(inputMajorNumber, INPUT_DEVICE_NAME);
    kfree(sharedBuffer);
    printk(KERN_INFO "Input device driver successfully removed.\n");
    return;
}

/*
 * Opens device module, sends appropriate message to kernel
 */
static int input_open(struct inode *inodep, struct file *filep)
{
	printk(KERN_INFO "Input device opened.\n");
	return 0;
}


/*
 * Closes device module, sends appropriate message to kernel
 */
static int input_close(struct inode *inodep, struct file *filep)
{
	printk(KERN_INFO "Input device closed.\n");
	return 0;
}


/*
 * Writes to the device
 */
static ssize_t input_write(struct file* file, const char* buffer, size_t length, loff_t* offset) {
    size_t bytesToCopy;
    size_t bytesWritten = 0;
    unsigned long flags;

    printk(KERN_INFO "Acquiring the lock\n");
    mutex_lock(&sharedBufferMutex);

    printk(KERN_INFO "In the Critical Section\n");
    while ((length > 0) && (*offset < SHARED_BUFFER_SIZE)) {
        bytesToCopy = min(length, (size_t)(SHARED_BUFFER_SIZE - *offset));
        if (bytesToCopy == 0) {
            break;
        }
        raw_spin_lock_irqsave((raw_spinlock_t *)&inputQueue.lock, flags);
        printk(KERN_INFO "Waiting on the lock\n");
        wait_event_interruptible_exclusive(inputQueue, (*offset < SHARED_BUFFER_SIZE));
        raw_spin_unlock_irqrestore((raw_spinlock_t *)&inputQueue.lock, flags);
        if (signal_pending(current)) {
	    mutex_unlock(&sharedBufferMutex);
	    printk(KERN_INFO "Failed to write\n");
	    return -EINTR;
	}
        if (copy_from_user(sharedBuffer + *offset, buffer + bytesWritten, bytesToCopy)) {
            mutex_unlock(&sharedBufferMutex);
            printk(KERN_INFO "Failed to read\n");
            return -EFAULT;
        }

        bytesWritten += bytesToCopy;
        *offset += bytesToCopy;
        length -= bytesToCopy;
        if (*offset == SHARED_BUFFER_SIZE) {
            printk(KERN_INFO "Buffer is full\n");
        }
    }

    mutex_unlock(&sharedBufferMutex);

    if (length > 0) {
        printk(KERN_INFO "Not enough space left in the buffer, dropping the rest.\n");
    } else {
        printk(KERN_INFO "Writing %zu bytes\n", bytesWritten);
    }
    return bytesWritten;

}



