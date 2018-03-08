#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fb.h>
#include <linux/console.h>

#define DRIVER_NAME "virtfb"

//
static struct fb_info *virtfb;
static struct fb_ops virtfb_ops = {
        .owner = THIS_MODULE,
};


int __init virtfb_init(void)
{
        struct fb_videomode m;
        pr_info("Virtual framebuffer init\n");
        virtfb = framebuffer_alloc(sizeof(unsigned int), NULL);
        if (!virtfb) return -ENOMEM;

        virtfb->var.activate = FB_ACTIVATE_NOW;
        virtfb->fbops = &virtfb_ops;
        virtfb->flags = FBINFO_FLAG_DEFAULT;

        sprintf(virtfb->fix.id,"virtfb1");
        
        virtfb->var.xres_virtual = virtfb->var.yres_virtual =
                virtfb->var.xres = virtfb->var.yres = 128;
        virtfb->var.bits_per_pixel = 16;

        virtfb->var.red.length = 5;
        virtfb->var.red.offset = 11;
        virtfb->var.red.msb_right = 0;

        virtfb->var.green.length = 6;
        virtfb->var.green.offset = 5;
        virtfb->var.green.msb_right = 0;

        virtfb->var.blue.length = 5;
        virtfb->var.blue.offset = 0;
        virtfb->var.blue.msb_right = 0;

        virtfb->var.transp.length = 0;
        virtfb->var.transp.offset = 0;
        virtfb->var.transp.msb_right = 0;

        virtfb->var.width = virtfb->var.height = -1;
        virtfb->var.grayscale = 0;

        virtfb->fix.type = FB_TYPE_PACKED_PIXELS;
        virtfb->fix.accel = FB_ACCEL_NONE;
        virtfb->fix.visual = FB_VISUAL_TRUECOLOR;

        if (!virtfb->modelist.next || !virtfb->modelist.prev)
                INIT_LIST_HEAD(&virtfb->modelist);

        fb_var_to_videomode(&m, &virtfb->var);
        fb_add_videomode(&m, &virtfb->modelist);

        virtfb->var.activate |= FB_ACTIVATE_FORCE;
        console_lock();
        virtfb->flags |= FBINFO_MISC_USEREVENT;
        fb_set_var(virtfb, &virtfb->var);
        virtfb->flags &= ~FBINFO_MISC_USEREVENT;
        console_unlock();

        register_framebuffer(virtfb);
        pr_info("Framebuffer registered\n");

        return 0;
}

void __exit virtfb_exit(void)
{
        pr_info("Virtual framebuffer exit\n");
        if (virtfb) {
                unregister_framebuffer(virtfb);
                framebuffer_release(virtfb);
        }
}
MODULE_LICENSE("GPL");
MODULE_AUTHOR("BRAVL <vlerickb@gmail.com");

module_init(virtfb_init);
module_exit(virtfb_exit);
