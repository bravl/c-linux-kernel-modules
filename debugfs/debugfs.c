#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>

static struct dentry *dir = 0;
static u32 hello = 0;

ssize_t debugfs_write(struct file *file, const char __user *buff, 
		      size_t count, loff_t *offset)
{
	printk("writing to a file\n");
	return count;
}

struct file_operations dbgfs_ops = {
	.write = debugfs_write,
};

int __init init_module(void)
{
	struct dentry *test;
	struct dentry *test_write;

	// Create a toplevel directory
	dir = debugfs_create_dir("debugtest",0);
	if (!dir) {
		printk(KERN_ALERT "Failed to create debugfs dir\n");
		return -1;
	}

	// create a simple debugfs file
	test = debugfs_create_u32("test",0666,dir,&hello);
	if (!test) {
		printk(KERN_ALERT "Failed to create file\n");
		return -1;
	}

	// create a debugfs with file_ops
	test_write = debugfs_create_file("write",0666,dir,NULL,&dbgfs_ops);
	if (!test_write) {
		printk(KERN_ALERT "Failed to create file\n");
		return -1;
	}

	return 0;
}

void __exit cleanup_module(void)
{
	debugfs_remove_recursive(dir);
}

MODULE_LICENSE("GPL");
