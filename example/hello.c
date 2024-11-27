#include <linux/module.h>
#include <linux/kernel.h>

int number_to_print = 42;

static int __init hello_module_init(void)
{
	printk(KERN_INFO "Hello world of modules (num: %d)\n", number_to_print);
	return 0;
}

static void __exit hello_module_exit(void)
{
	printk(KERN_INFO "Bye bye world of modules (num: %d)\n", number_to_print);
}

MODULE_AUTHOR("Dimitris Siakavaras");
MODULE_LICENSE("GPL");

module_param(number_to_print, int, 0);
MODULE_PARM_DESC(number_to_print, "Number to print with our message");

module_init(hello_module_init);
module_exit(hello_module_exit);
