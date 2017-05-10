#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init_task.h>

MODULE_AUTHOR("BRAVL");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Scheduler Backtrace");
MODULE_VERSION("1.0");

void print_task(struct task_struct *task) {
	struct task_struct *child;
	struct list_head *list;

	printk("name: %s, pid: [%d], state: %li\n", task->comm, task->pid, task->state);
	list_for_each(list, &task->children) {
		child = list_entry(list, struct task_struct, sibling);
		print_task(child);
	}
}

static int __init module_load(void) {
	print_task(&init_task);
	return 0;
}

static void __exit module_unload(void) {
	printk("Exiting");
	return;
}

module_init(module_load);
module_exit(module_unload);
