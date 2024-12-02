/*
 * lunix-chrdev.c
 *
 * Implementation of character devices
 * for Lunix:TNG
 *
 * < Nikolaos Roumpies && Dimitrios Dimitrakopoulos >
 *
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/ioctl.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mmzone.h>
#include <linux/vmalloc.h>
#include <linux/spinlock.h>

#include "lunix.h"
#include "lunix-chrdev.h"
#include "lunix-lookup.h"

/*
 * Global data
 */
struct cdev lunix_chrdev_cdev;

/*
 * Just a quick [unlocked] check to see if the cached
 * chrdev state needs to be updated from sensor measurements.
 */
/*
 * Declare a prototype so we can define the "unused" attribute and keep
 * the compiler happy. This function is not yet used, because this helpcode
 * is a stub.
 */
static int __attribute__((unused)) lunix_chrdev_state_needs_refresh(struct lunix_chrdev_state_struct *);
static int lunix_chrdev_state_needs_refresh(struct lunix_chrdev_state_struct *state)
{
	struct lunix_sensor_struct *sensor;
	
	WARN_ON ( !(sensor = state->sensor));
	/* ? */

	/* The following return is bogus, just for the stub to compile */
	return 0; /* ? */
}

/*
 * Updates the cached state of a character device
 * based on sensor data. Must be called with the
 * character device state lock held.
 */
static int lunix_chrdev_state_update(struct lunix_chrdev_state_struct *state)
{
    struct lunix_sensor_struct *sensor = state->sensor;
    
    // Acquire lock or perform necessary synchronization
    spin_lock(&sensor->lock);

    // Fetch the sensor's data (for example, from hardware or memory)
    if (!lunix_fetch_sensor_data(sensor)) {
        spin_unlock(&sensor->lock);
        return -EAGAIN;  // No new data available, retry later
    }

    // Store data into the state structure
    state->data = sensor->data;
    state->data_len = sensor->data_len;
    state->data_ready = true;

    // Release lock
    spin_unlock(&sensor->lock);
    
    return 0;
}


/*************************************
 * Implementation of file operations
 * for the Lunix character device
 *************************************/

static int lunix_chrdev_open(struct inode *inode, struct file *filp)
{
    int ret = -ENODEV;
    struct lunix_chrdev_state_struct *state;

    debug("entering\n");

    // Check for valid sensor state
    state = kzalloc(sizeof(struct lunix_chrdev_state_struct), GFP_KERNEL);
    if (!state)
        return -ENOMEM;

    // Store private data for the file (sensor state)
    filp->private_data = state;

    // Associate the device minor number with a sensor
    state->sensor = lunix_get_sensor_from_minor(inode->i_rdev);
    if (!state->sensor) {
        ret = -ENODEV;
        goto out;
    }

    ret = nonseekable_open(inode, filp);
out:
    debug("leaving, with ret = %d\n", ret);
    return ret;
}

static int lunix_chrdev_release(struct inode *inode, struct file *filp)
{
    struct lunix_chrdev_state_struct *state = filp->private_data;
    
    // Free state structure
    kfree(state);

    debug("release complete\n");
    return 0;
}


static int lunix_chrdev_release(struct inode *inode, struct file *filp)
{
	/* ? */
	return 0;
}

static long lunix_chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

static ssize_t lunix_chrdev_read(struct file *filp, char __user *usrbuf, size_t cnt, loff_t *f_pos)
{
    ssize_t ret = 0;
    struct lunix_chrdev_state_struct *state = filp->private_data;
    struct lunix_sensor_struct *sensor;
    
    sensor = state->sensor;
    WARN_ON(!sensor);

    // If the offset is at the beginning, update the device state
    if (*f_pos == 0) {
        while (lunix_chrdev_state_update(state) == -EAGAIN) {
            // Sleep while waiting for fresh sensor data
            if (filp->f_flags & O_NONBLOCK) {
                ret = -EAGAIN;
                goto out;
            }
            wait_event_interruptible(sensor->wait_queue, state->data_ready);
        }
    }

    // Here, you would prepare the data from the sensor
    ret = copy_to_user(usrbuf, state->data, min(cnt, state->data_len));
    if (ret) {
        ret = -EFAULT;
        goto out;
    }

    *f_pos += ret;  // Update file position

out:
    return ret;
}


static int lunix_chrdev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	return -EINVAL;
}

static struct file_operations lunix_chrdev_fops = 
{
	.owner          = THIS_MODULE,
	.open           = lunix_chrdev_open,
	.release        = lunix_chrdev_release,
	.read           = lunix_chrdev_read,
	.unlocked_ioctl = lunix_chrdev_ioctl,
	.mmap           = lunix_chrdev_mmap
};

int lunix_chrdev_init(void)
{
    int ret;
    dev_t dev_no;
    unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;  // Adjust minor count based on sensor count

    debug("initializing character device\n");

    // Initialize the character device structure
    cdev_init(&lunix_chrdev_cdev, &lunix_chrdev_fops);
    lunix_chrdev_cdev.owner = THIS_MODULE;

    // Create a device number (major, minor)
    dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);

    // Register the character device region (reserve major number and minor number range)
    ret = register_chrdev_region(dev_no, lunix_minor_cnt, "lunix");
    if (ret < 0) {
        debug("failed to register region, ret = %d\n", ret);
        return ret;
    }

    // Add the character device to the system
    ret = cdev_add(&lunix_chrdev_cdev, dev_no, lunix_minor_cnt);
    if (ret < 0) {
        debug("failed to add character device, ret = %d\n", ret);
        unregister_chrdev_region(dev_no, lunix_minor_cnt);
        return ret;
    }

    debug("device initialized successfully\n");
    return 0;
}

void lunix_chrdev_destroy(void)
{
    dev_t dev_no;
    unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;

    debug("entering\n");

    // Get device number and clean up
    dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);
    cdev_del(&lunix_chrdev_cdev);
    unregister_chrdev_region(dev_no, lunix_minor_cnt);

    debug("leaving\n");
}
