#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

MODULE_AUTHOR("BRAVL");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Qemu PCI driver");
MODULE_VERSION("1.0");

static const struct pci_device_id pcidevtbl[] = {
        {0x1234, 0x0001, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
};

static int __init pci_qemu_module_init(void)
{
        int err = 0;

        return err;
}

static void __exit pci_qemu_module_exit(void)
{

}

module_init(pci_qemu_module_init)
module_exit(pci_qemu_module_exit)
