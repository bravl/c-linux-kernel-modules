#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>

static struct dentry *dir = 0;
static u32 hello = 0;

int __init init_module(void)
{
	struct dentry *junk;
	dir = debugfs_create_dir("debugtest",0);
	if (!dir) {
		printk(KERN_ALERT "Failed to create debugfs dir\n");
		return -1;
	}

	junk = debugfs_create_u32("test",0666,dir,&hello);
	if (!junk) {
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
