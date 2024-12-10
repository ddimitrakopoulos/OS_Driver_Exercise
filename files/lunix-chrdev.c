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

//is called to check whether a lunix sensor needs to be updated (returns 1) or not (return 0)
//is only called inside lunix_chrdev_state_update (?)
static int lunix_chrdev_state_needs_refresh(struct lunix_chrdev_state_struct *state)
{
    // struct sensor to copy the sensor of the state argument
	struct lunix_sensor_struct *sensor;
    // macro that is used for debugging and throws a warning if the state->sensor is null
	WARN_ON ( !(sensor = state->sensor));
    // if the last update for the specific sensor was more recently than the last timestamp we have saved we need to update the sensor
	if(sensor->msr_data[state->type]->last_update > state->buf_timestamp)
	{
		debug("I need refreshing\n");
		return(1);
	}	
	debug("Im good\n");
	return 0; 
}

/*
 * Updates the cached state of a character device
 * based on sensor data. Must be called with the
 * character device state lock held.
 */
static int lunix_chrdev_state_update(struct lunix_chrdev_state_struct *state)
{
	unsigned long flags; //this is used to save the state when calling spin lock/unlock irq save 
	struct lunix_sensor_struct *sensor;
	uint16_t raw_data;
	uint32_t time;
	long measurement;
	debug("leaving\n");
	sensor = state->sensor;
	/*
	 * Grab the raw data quickly, hold the
	 * spinlock for as little as possible.
	 */

	spin_lock_irqsave(&sensor->lock, flags);//In flags the flag goes register, no hard interrupts
	/*Update buf_data by loading the data of 1 sensor*/
	raw_data = sensor->msr_data[state->type]->values[0]; //save data and last update time
	time = sensor->msr_data[state->type]->last_update; 
	spin_unlock_irqrestore(&sensor->lock,flags);
	
	/*
	 * Now we can take our time to format them,
	 * holding only the private state semaphore
	 */
	
	if(lunix_chrdev_state_needs_refresh(state)) //if data have been refreshed before last update then update timestamp, transform the value into the desired format
    //and write into the buffer so we can read it when needed 
	{
		state -> buf_timestamp = time; //buf_timestamp is time of last update
		switch (state->type) {
			case BATT:
				measurement = lookup_voltage[raw_data];
				break;
			case TEMP:
				measurement = lookup_temperature[raw_data];
				break;
			case LIGHT:
				measurement = lookup_light[raw_data];
				break;
			default:
				return -EINVAL;
		}

    		state->buf_lim = snprintf(state->buf_data, LUNIX_CHRDEV_BUFSZ, " %ld.%03ld\n", measurement / 1000, measurement % 1000);
	}
	else
	{
		debug("Nothing to refresh\n");
		return -EAGAIN;
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
	//lunix_chrdev_init();
	struct lunix_chrdev_state_struct *state;
	int ret;
	unsigned int min_num, sensor_num;

	debug("entering\n");
	ret = -ENODEV;
	if ((ret = nonseekable_open(inode, filp)) < 0)
	{
		goto out;
	}

	state = kmalloc(sizeof(*state), GFP_KERNEL);
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
	sensor_num = min_num >> 3; //take sensor num based on inode
	state->sensor = &lunix_sensors[sensor_num]; //Sensor in state points to sensor state struct
	min_num = min_num & 0x07; //keep last 3 bits to find the type
	state->type = min_num;	
	
	state->buf_timestamp = 0;
	state->buf_lim = 0;
	memset(&state->buf_data,0,20);

	sema_init(&state->lock,1);
	
	/* Allocate a new Lunix character device private state structure */
	filp->private_data = state; //connect fd to struct
	ret = 0;
out:
	debug("leaving, with ret = %d\n", ret);
	return ret;
}

//is called when a user space application (like cat) uses the close syscall 
static int lunix_chrdev_release(struct inode *inode, struct file *filp)
{
	kfree(filp->private_data); //free the memory of the pre-opened file 
	return 0;
}

static long lunix_chrdev_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return -EINVAL;
}

static ssize_t lunix_chrdev_read(struct file *filp, char __user *usrbuf, size_t cnt, loff_t *f_pos)
{
	ssize_t ret;
	
	struct lunix_sensor_struct *sensor;
	struct lunix_chrdev_state_struct *state;

	state = filp->private_data;
	WARN_ON(!state);

	sensor = state->sensor;
	WARN_ON(!sensor);

	if(down_interruptible(&state->lock))
	{
		ret = -EAGAIN;
		goto out;
	}
	/*
	 * If the cached character device state needs to be
	 * updated by actual sensor data (i.e. we need to report
	 * on a "fresh" measurement, do so
	 */
	if(*f_pos == 0) {
		while (lunix_chrdev_state_update(state) == -EAGAIN) {
            /* Release lock while sleeping */
            up(&state->lock);

            
            /* Wait for sensor data to become available */
            if (wait_event_interruptible(sensor->wq, lunix_chrdev_state_needs_refresh(state))) //Non-Blocking op
                return -ERESTARTSYS;

            /* Reacquire the lock */
            if (down_interruptible(&state->lock))
                return -ERESTARTSYS;
        }

	}
	
	cnt = min(cnt,(size_t)(state->buf_lim - *f_pos));
	
	/* End of file */
	ret = cnt; // - *f_pos;

	if(copy_to_user(usrbuf,state->buf_data + *f_pos,cnt))
	{
		ret = -EFAULT;
		goto out;
	}
	/* Auto-rewind on EOF mode? */
	if(cnt == state->buf_lim)
	{
		*f_pos = 0;
		goto out;
	}
	else
	{       	
		*f_pos = *f_pos + cnt;
	}
	/*
	 * The next two lines  are just meant to suppress a compiler warning
	 * for the "unused" out: label, and for the uninitialized "ret" value.
	 * It's true, this helpcode is a stub, and doesn't use them properly.
	 * Remove them when you've started working on this code.
	 */
out:
	up(&state->lock);
	//wake_up_interruptible(&sensor->wq);
	debug("%d out with bytes %li\n", state->type, (long)cnt);
	return ret;
}

static int lunix_chrdev_mmap(struct file *filp, struct vm_area_struct *vma)
{
	return -EINVAL;
}

//tells the kernel how to handle system calls (ex. open calls lunix_chrdev_open)
static struct file_operations lunix_chrdev_fops = 
{
	.owner          = THIS_MODULE,
	.open           = lunix_chrdev_open,
	.release        = lunix_chrdev_release,
	.read           = lunix_chrdev_read,
	.unlocked_ioctl = lunix_chrdev_ioctl,
	.mmap           = lunix_chrdev_mmap
};

//this function is called when calling the command insmod ~/path/to/lunix.ko
int lunix_chrdev_init(void)
{
	/*
	 * Register the character device with the kernel, asking for
	 * a range of minor numbers (number of sensors * 8 measurements / sensor)
	 * beginning with LINUX_CHRDEV_MAJOR:0
	 */ 
	int ret;
	dev_t dev_no;
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;

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

//this function is called when running the command rmmod lunix
void lunix_chrdev_destroy(void)
{
	dev_t dev_no;
	unsigned int lunix_minor_cnt = lunix_sensor_cnt << 3;

	debug("entering destroy\n");
	dev_no = MKDEV(LUNIX_CHRDEV_MAJOR, 0);
	cdev_del(&lunix_chrdev_cdev);
	unregister_chrdev_region(dev_no, lunix_minor_cnt);
	debug("leaving destroy\n");
}
