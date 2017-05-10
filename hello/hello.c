#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_AUTHOR("BRAVL");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hello");
MODULE_VERSION("1.0");

static int __init module_load(void) {
	printk("Hello World!\n");
	return 0;
}

static void __exit module_unload(void) {
	printk("Bye Bye World!\n");
	return;
}

module_init(module_load);
module_exit(module_unload);
