/*
 * chrdev.c
 *
 * A simple character device to showcase the usage of
 * wait_event_interruptible() and wake_event_interruptible().
 *
 * Dimitris Siakavaras <jimsiak@cslab.ece.ntua.gr>
 *
 */
#include <linux/cdev.h>

#define WW_CHRDEV_MAJOR 60

int ww_chrdev_init(void);
void ww_chrdev_destroy(void);

struct cdev ww_chrdev_cdev;
wait_queue_head_t wq;

unsigned int last_wakeup;

static int ww_chrdev_open(struct inode *inode, struct file *filp)
{
	int ret;

	pr_info("%s: Entering\n", __func__);
	ret = -ENODEV;
	if ((ret = nonseekable_open(inode, filp)) < 0)
		goto out;

	filp->private_data = (void *)(unsigned long)iminor(inode);

out:
	pr_info("%s: Leaving\n", __func__);
	return ret;
}

static int ww_chrdev_release(struct inode *inode, struct file *filp)
{
	pr_info("%s: Entering\n", __func__);
	pr_info("%s: Leaving\n", __func__);
	return 0;
}

static ssize_t ww_chrdev_read(struct file *filp, char __user *usrbuf, size_t cnt, loff_t *f_pos)
{
	unsigned int minor;
	unsigned int sleep_time;

	pr_info("%s: Entering\n", __func__);

	minor = (unsigned int)(unsigned long)filp->private_data;
	pr_info("Minor number is %u\n", minor);

	switch (minor) {
	case 0:
		// wait file is being read
		pr_info("%u: I am going to sleep now\n", current->pid);
		sleep_time = ktime_get_real_seconds();
		wait_event_interruptible(wq, last_wakeup >= sleep_time);
		pr_info("%u: I just woke up\n", current->pid);
		break;
	case 1:
		// wake file is being read
		pr_info("%u: I am going to wake up all sleepers\n", current->pid);
		last_wakeup = ktime_get_real_seconds();
		wake_up_interruptible(&wq);
		pr_info("%u: I woke up everyone\n", current->pid);
		break;
	default:
		pr_err("Unknown minor number\n");
	}

	pr_info("%s: Leaving\n", __func__);
	return 0;
}

static struct file_operations ww_chrdev_fops = 
{
	.owner          = THIS_MODULE,
	.open           = ww_chrdev_open,
	.release        = ww_chrdev_release,
	.read           = ww_chrdev_read,
};

int ww_chrdev_init(void)
{
	int ret;
	dev_t dev_no;
	unsigned int minor_cnt = 2;

	pr_info("%s: Entering\n", __func__);
	cdev_init(&ww_chrdev_cdev, &ww_chrdev_fops);
	ww_chrdev_cdev.owner = THIS_MODULE;
	
	dev_no = MKDEV(WW_CHRDEV_MAJOR, 0);
	ret = register_chrdev_region(dev_no, minor_cnt, "wait-wake");
	if (ret < 0) {
		pr_err("failed to register region, ret = %d\n", ret);
		goto out;
	}

	ret = cdev_add(&ww_chrdev_cdev, dev_no, minor_cnt);
	if (ret < 0) {
		pr_err("failed to add character device\n");
		goto out_with_chrdev_region;
	}

	init_waitqueue_head(&wq);
	last_wakeup = ktime_get_real_seconds();

	pr_info("%s: Leaving\n", __func__);
	return 0;

out_with_chrdev_region:
	unregister_chrdev_region(dev_no, minor_cnt);
out:
	return ret;
}

void ww_chrdev_destroy(void)
{
	dev_t dev_no;
	unsigned int minor_cnt = 2;

	pr_info("%s: Entering\n", __func__);
	dev_no = MKDEV(WW_CHRDEV_MAJOR, 0);
	cdev_del(&ww_chrdev_cdev);
	unregister_chrdev_region(dev_no, minor_cnt);
	pr_info("%s: Leaving\n", __func__);
}
