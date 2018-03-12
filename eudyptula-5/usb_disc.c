#include <linux/usb.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/hid.h>

static struct usb_device_id id_table[] = {
	{USB_INTERFACE_INFO(USB_INTERFACE_CLASS_HID,
			USB_INTERFACE_SUBCLASS_BOOT,
			USB_INTERFACE_PROTOCOL_KEYBOARD)},
	{},
};

MODULE_DEVICE_TABLE(usb, id_table);

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

MODULE_AUTHOR("BRAVL");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Hello");
MODULE_VERSION("1.0");


