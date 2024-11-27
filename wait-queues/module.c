/*
 * module.c
 *
 * Simple module to showcase the use of wait_event_interruptible() and
 * wake_up_interruptible().
 *
 * Dimitris Siakavaras <jimsiak@cslab.ece.ntua.gr>
 *
 */
#include <linux/module.h>

int ww_chrdev_init(void);
void ww_chrdev_destroy(void);

static int __init ww_module_init(void)
{
	int ret = 0;
	pr_info("Initializing the wait/wake module\n");
	ret = ww_chrdev_init();
	return ret;
}

static void __exit ww_module_cleanup(void)
{
	pr_info("Removing the wait/wake module from the kernel\n");
	ww_chrdev_destroy();
}

MODULE_AUTHOR("Dimitris Siakavaras");
MODULE_LICENSE("GPL");

module_init(ww_module_init);
module_exit(ww_module_cleanup);
