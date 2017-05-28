#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_AUTHOR("BRAVL");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hello");
MODULE_VERSION("1.0");

static int __init module_load(void)
{
	pr_info("Hello World!\n");
	return 0;
}

static void __exit module_unload(void)
{
	pr_info("Bye Bye World!\n");
}

module_init(module_load);
module_exit(module_unload);
