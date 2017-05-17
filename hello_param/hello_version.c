#include <linux/init.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/utsname.h>
#include <linux/timekeeping.h>

/* Add your code here */

static char *param = "test";
module_param(param,charp,0);

static int __init run_module(void)
{
	struct timeval tv;
	pr_info("Hello world %s\n",param);
	pr_info("Version: %s %s\n", utsname()->release,utsname()->version);
	do_gettimeofday(&tv);
	pr_info("Time elapsed %ld\n",tv.tv_sec);
	return 0;
}

static void __exit close_module(void)
{
	pr_info("Exit module\n");
}

module_init(run_module);
module_exit(close_module);

MODULE_LICENSE("GPL");

