#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/device.h>
#include <linux/cdev.h>

#define DEVICE_NAME "lunix"  // Base device name
#define CLASS_NAME "lunix_class"  // Class name for /dev
#define MAX_FILENAME 256  // Max filename length

// Global variables
static int major_number;
static struct class *lunix_class = NULL;
static struct device *lunix_device = NULL;
static struct cdev lunix_cdev;  // Character device structure

// Data types for our devices
enum device_type { BATTERY, LIGHT, TEMPERATURE };

// Function prototypes
static int dev_open(struct inode *inode, struct file *file);
static ssize_t dev_write(struct file *file, const char *buffer, size_t len, loff_t *offset);
static ssize_t dev_read(struct file *file, char *buffer, size_t len, loff_t *offset);
static int dev_release(struct inode *inode, struct file *file);

// File operations structure
static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};

// Create device files dynamically for battery, light, and temperature
void create_files(int X) {
    char path[MAX_FILENAME];

    // Create file for battery
    snprintf(path, MAX_FILENAME, "/dev/lunix%d-bat", X);
    printk(KERN_INFO "Device file created at %s\n", path);

    // Create file for light
    snprintf(path, MAX_FILENAME, "/dev/lunix%d-light", X);
    printk(KERN_INFO "Device file created at %s\n", path);

    // Create file for temperature
    snprintf(path, MAX_FILENAME, "/dev/lunix%d-temp", X);
    printk(KERN_INFO "Device file created at %s\n", path);
}

// Writing data into the device file
static ssize_t dev_write(struct file *file, const char *buffer, size_t len, loff_t *offset) {
    char *kernel_buffer;
    size_t max_size = 256;
    enum device_type dev_type;

    // Allocate memory for kernel space buffer
    kernel_buffer = kzalloc(max_size, GFP_KERNEL);
    if (!kernel_buffer) {
        printk(KERN_ERR "Failed to allocate memory\n");
        return -ENOMEM;
    }

    // Determine which device type we're interacting with
    if (file->f_inode->i_rdev == MKDEV(major_number, BATTERY)) {
        dev_type = BATTERY;
    } else if (file->f_inode->i_rdev == MKDEV(major_number, LIGHT)) {
        dev_type = LIGHT;
    } else if (file->f_inode->i_rdev == MKDEV(major_number, TEMPERATURE)) {
        dev_type = TEMPERATURE;
    }

    // Copy data from user-space to kernel-space
    if (copy_from_user(kernel_buffer, buffer, len)) {
        kfree(kernel_buffer);
        return -EFAULT;
    }

    // Process based on the device type
    switch (dev_type) {
    case BATTERY:
        printk(KERN_INFO "Written to battery device: %s\n", kernel_buffer);
        break;
    case LIGHT:
        printk(KERN_INFO "Written to light device: %s\n", kernel_buffer);
        break;
    case TEMPERATURE:
        printk(KERN_INFO "Written to temperature device: %s\n", kernel_buffer);
        break;
    }

    kfree(kernel_buffer);  // Free allocated memory
    return len;  // Return number of bytes written
}

// Reading data from the device file (example)
static ssize_t dev_read(struct file *file, char *buffer, size_t len, loff_t *offset) {
    const char *data;
    size_t data_len;

    // Determine which data type to read
    if (file->f_inode->i_rdev == MKDEV(major_number, BATTERY)) {
        data = "Device data: Battery: 27.90%";
    } else if (file->f_inode->i_rdev == MKDEV(major_number, LIGHT)) {
        data = "Device data: Light: 80.5%";
    } else if (file->f_inode->i_rdev == MKDEV(major_number, TEMPERATURE)) {
        data = "Device data: Temperature: 22.5°C";
    } else {
        return -EINVAL;  // Invalid device type
    }

    data_len = strlen(data);

    // Copy data from kernel-space to user-space
    if (copy_to_user(buffer, data, data_len)) {
        return -EFAULT;
    }

    printk(KERN_INFO "Sent data: %s\n", data);
    return data_len;  // Return number of bytes read
}

// Open the device
static int dev_open(struct inode *inode, struct file *file) {
    printk(KERN_INFO "Device opened\n");
    return 0;
}

// Release the device
static int dev_release(struct inode *inode, struct file *file) {
    printk(KERN_INFO "Device released\n");
    return 0;
}

// Module initialization function
static int __init lunix_init(void) {
    printk(KERN_INFO "Lunix driver loaded\n");

    // Dynamically allocate a major number
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ERR "Failed to register a major number\n");
        return major_number;
    }

    // Register the device class
    lunix_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(lunix_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ERR "Failed to register device class\n");
        return PTR_ERR(lunix_class);
    }

    // Register the device driver for each data type (battery, light, temp)
    lunix_device = device_create(lunix_class, NULL, MKDEV(major_number, BATTERY), NULL, "lunix-bat");
    if (IS_ERR(lunix_device)) {
        class_destroy(lunix_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ERR "Failed to create the battery device\n");
        return PTR_ERR(lunix_device);
    }

    lunix_device = device_create(lunix_class, NULL, MKDEV(major_number, LIGHT), NULL, "lunix-light");
    if (IS_ERR(lunix_device)) {
        class_destroy(lunix_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ERR "Failed to create the light device\n");
        return PTR_ERR(lunix_device);
    }

    lunix_device = device_create(lunix_class, NULL, MKDEV(major_number, TEMPERATURE), NULL, "lunix-temp");
    if (IS_ERR(lunix_device)) {
        class_destroy(lunix_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ERR "Failed to create the temperature device\n");
        return PTR_ERR(lunix_device);
    }

    // Create the files as needed
    create_files(1);  // Create lunix1-bat, lunix1-light, lunix1-temp

    printk(KERN_INFO "Lunix device created successfully\n");
    return 0;
}

// Module exit function
static void __exit lunix_exit(void) {
    printk(KERN_INFO "Lunix driver unloaded\n");

    // Clean up devices
    device_destroy(lunix_class, MKDEV(major_number, BATTERY));
    device_destroy(lunix_class, MKDEV(major_number, LIGHT));
    device_destroy(lunix_class, MKDEV(major_number, TEMPERATURE));

    // Clean up class and unregister chrdev
    class_destroy(lunix_class);
    unregister_chrdev(major_number, DEVICE_NAME);
}

module_init(lunix_init);
module_exit(lunix_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dimitrios Dimitrakopoulos && Nikolaos Roumpies");
MODULE_DESCRIPTION("A simple Linux driver for lunixX-bat, lunixX-light, and lunixX-temp");
MODULE_VERSION("0.1");
