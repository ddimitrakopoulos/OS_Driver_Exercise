/*
 * lunix-chrdev.c
 *
 * Implementation of character devices
 * for Lunix:TNG
 *
 * < Dimitrakopoulos Dimitrios - Roumpies Nikolaos >
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
//static int __attribute__((unused)) lunix_chrdev_state_needs_refresh(struct lunix_chrdev_state_struct *);
static int lunix_chrdev_state_needs_refresh(struct lunix_chrdev_state_struct *state)
{
	struct lunix_sensor_struct *sensor;
	WARN_ON ( !(sensor = state->sensor));
	if(sensor->msr_data[state->type]->last_update > state->buf_timestamp)
	{
		return(1);//if last update happend later than the last return 1 
	}	
	
	return 0; //else 0
}

/*
 * Updates the cached state of a character device
 * based on sensor data. Must be called with the
 * character device state lock held.
 */
static int lunix_chrdev_state_update(struct lunix_chrdev_state_struct *state)
{
	unsigned long flags;
	struct lunix_sensor_struct *sensor;
	uint16_t raw_data;
	debug("leaving\n");
	sensor = state->sensor;
	/*
	 * Grab the raw data quickly, hold the
	 * spinlock for as little as possible.
	 */

	spin_lock_irqsave(&sensor->lock, flags);
	/*Update buf_data by loading the data of 1 sensor*/
	raw_data = sensor->msr_data[state->type]->values[0];
	state->buf_timestamp = ktime_get_real_seconds();
	spin_unlock_irqrestore(&sensor->lock,flags);
	
	/*
	 * Now we can take our time to format them,
	 * holding only the private state semaphore
	 */
	
	if(lunix_chrdev_state_needs_refresh(state))
	{
		if(state->type == 0)
		{
			raw_data = lookup_voltage[raw_data];
			 snprintf(state->buf_data,20,"%.3d",raw_data);
		}
		if(state->type == 1)
		{
			raw_data = lookup_temperature[raw_data];
			 snprintf(state->buf_data,20,"%.3d",raw_data);
		}
		if(state->type == 2)
		{
			raw_data = lookup_light[raw_data];
			 snprintf(state->buf_data,20,"%.3d",raw_data);
		}
	}

	debug("leaving\n");
	return 0;
}

/*************************************
 * Implementation of file operations
 * for the Lunix character device
 *************************************/

static int lunix_chrdev_open(struct inode *inode, struct file *filp)
{
	/* Declarations */
	struct lunix_chrdev_state_struct *state;
	int ret;
	unsigned int min_num, sensor_num;

	debug("entering\n");
	ret = -ENODEV;
	if ((ret = nonseekable_open(inode, filp)) < 0)
		goto out;

	state = kmalloc(sizeof(state), GFP_KERNEL);
	if (!state) 
	{
    		debug("Memory allocation failed\n");
    		ret= -ENOMEM;
		goto out;
	}
	/*
	 * Associate this open file with the relevant sensor based on
	 * the minor number of the device node [/dev/sensor<NO>-<TYPE>]
	 */
	min_num = iminor(inode);
	sensor_num = min_num >> 3;
	state->sensor = &lunix_sensors[sensor_num]; 
	min_num = min_num & 0x07;
	state->type = min_num;	
	
	state->buf_timestamp = 0;

	memset(&state->buf_data,0,20);

	sema_init(&state->lock,1);
	
	/* Allocate a new Lunix character device private state structure */
	filp->private_data = state;
	ret = 0;
out:
	debug("leaving, with ret = %d\n", ret);
	return ret;
}

static int lunix_chrdev_release(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data);//private data points to each state so probably works	
	return 0;
}

static long lunix_chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

static ssize_t lunix_chrdev_read(struct file *filp, char __user *usrbuf, size_t cnt, loff_t *f_pos)
{
    struct lunix_chrdev_state_struct *state;
    struct lunix_sensor_struct *sensor;
    ssize_t ret;
    size_t remaining_bytes, bytes_to_copy;

    debug("entering\n");

    /* Retrieve the device state */
    state = filp->private_data;
    WARN_ON(!state);

    /* Retrieve the associated sensor */
    sensor = state->sensor;
    WARN_ON(!sensor);

    /* Acquire semaphore to ensure exclusive access */
    if (down_interruptible(&state->lock))
        return -ERESTARTSYS;

    /* Check if cached state needs updating */
    if (*f_pos == 0) {
        while (lunix_chrdev_state_update(state) == -EAGAIN) {
            /* Release lock while sleeping */
            up(&state->lock);

            /* Handle non-blocking mode */
            if (filp->f_flags & O_NONBLOCK)
                return -EAGAIN;

            /* Wait for sensor data to become available */
            if (wait_event_interruptible(sensor->wq, lunix_chrdev_state_needs_refresh(state)))
                return -ERESTARTSYS;

            /* Reacquire the lock */
            if (down_interruptible(&state->lock))
                return -ERESTARTSYS;
        }
    }

    /* Calculate bytes available to read */
    remaining_bytes = state->buf_lim - *f_pos;
    bytes_to_copy = min(cnt, remaining_bytes);

    if (!bytes_to_copy) { /* End of file */
        ret = 0;
        goto out;
    }

    /* Copy data from the buffer to user space */
    if (copy_to_user(usrbuf, state->buf_data + *f_pos, bytes_to_copy)) {
        ret = -EFAULT;
        goto out;
    }

    /* Update file position and return number of bytes read */
    *f_pos += bytes_to_copy;
    ret = bytes_to_copy;

    /* Handle auto-rewind on EOF */
    if (*f_pos == state->buf_lim)
        *f_pos = 0;

out:
    /* Release the semaphore */
    up(&state->lock);
    debug("leaving\n");
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
	/*
	 * Register the character device with the kernel, asking for
	 * a range of minor numbers (number of sensors * 8 measurements / sensor)
	 * beginning with LINUX_CHRDEV_MAJOR:0
	 */ 
	//struct lunix_sensor_struct *s;
	int ret;
	dev_t dev_no;
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;
	/*Initialize stuff*/
	//spin_lock_init(&s->lock);
	//init_waitqueue_head(&s->wq);

	debug("initializing character device\n");
	cdev_init(&lunix_chrdev_cdev, &lunix_chrdev_fops);
	lunix_chrdev_cdev.owner = THIS_MODULE;
	
	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);
	
	/* register_chrdev_region */
	ret = register_chrdev_region(dev_no, lunix_minor_cnt, "lunix");
	if (ret < 0) {
		debug("failed to register region, ret = %d\n", ret);
		goto out;
	}
	
	/* cdev_add. Adding char device to kernel*/
	ret = cdev_add(&lunix_chrdev_cdev, dev_no, lunix_minor_cnt);
	if (ret < 0) {
		debug("failed to add character device\n");
		goto out_with_chrdev_region;
	}
	debug("completed successfully\n");
	return 0;

out_with_chrdev_region:
	unregister_chrdev_region(dev_no, lunix_minor_cnt);
out:
	return ret;
}

void lunix_chrdev_destroy(void)
{
	dev_t dev_no;
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;

	debug("entering\n");
	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);
	cdev_del(&lunix_chrdev_cdev);
	unregister_chrdev_region(dev_no, lunix_minor_cnt);
	debug("leaving\n");
}
