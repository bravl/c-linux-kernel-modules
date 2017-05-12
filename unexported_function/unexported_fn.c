#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init_task.h>

MODULE_AUTHOR("BRAVL");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Calling unexported function");
MODULE_VERSION("1.0");

typedef void (*fn)(unsigned long);

static int __init module_load(void) {
	fn func = 0xffffffff8105d196;
	func(0);
	return 0;
}

static void __exit module_unload(void) {
	printk("Exiting");
	return;
}

module_init(module_load);
module_exit(module_unload);
