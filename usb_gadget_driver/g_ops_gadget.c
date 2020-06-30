/**
 * @file g_ops_gadget.c
 * @brief  Simple USB Composite driver test
 * @author Bram Vlerick <bram.vlerick@openpixelsystems.org>
 * @version v0.1
 * @date 2020-06-30
 */

#include <linux/kernel.h>
#include <linux/usb/ch9.h>

#include "composite.c"
#include "usbstring.c"
#include "config.c"
#include "epautoconf.c"

#include "f_mass_storage.c"

static int __init gops_do_config(struct usb_configuration *c)
{

}

static int __init gops_bind(struct usb_composite_dev *cdev)
{

}

static struct usb_composite_driver gops_device = {
	.name = "OPS Gadget device",
	.dev = &gops_descriptor,
	.bind = gops_bind,
};

static int __init gops_init(void)
{
	return usb_composite_register(&gops_device);
}
module_init(gops_init);

static int gops_exit(void)
{
	usb_composite_deregister(&gops_device);
}
module_exit(gops_exit);

MODULE_DESCRIPTION("Simple USB Gadget device");
MODULE_AUTHOR("Bram Vlerick");
MODULE_LICENSE("GPL");
