#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fb.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/init.h>

#define DRIVER_NAME "virtfb"

static int virtfb_mmap(struct fb_info *info, struct vm_area_struct *vma);
static int virtfb_set_par(struct fb_info *info);
static int virtfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info);

static const struct fb_var_screeninfo default_var = {
        .xres_virtual = 128,
        .yres_virtual = 128, 
        .xres = 128,
        .yres = 128,
        .bits_per_pixel = 24,
        .red.length = 5,
        .red.offset = 11,
        .red.msb_right = 0,
        .green.length = 6,
        .green.offset = 5,
        .green.msb_right = 0,
        .blue.length = 5,
        .blue.offset = 0,
        .blue.msb_right = 0,
        .transp.length = 0,
        .transp.offset = 0,
        .transp.msb_right = 0,
        .width = -1,
        .height = -1,
        .grayscale = 0,
        .activate = FB_ACTIVATE_NOW,
};

static const struct fb_fix_screeninfo default_fix = {
        .type = FB_TYPE_PACKED_PIXELS,
        .accel = FB_ACCEL_NONE,
        .visual = FB_VISUAL_TRUECOLOR,
};

static struct fb_ops virtfb_ops = {
        .owner = THIS_MODULE,
        .fb_mmap = virtfb_mmap,
        .fb_set_par = virtfb_set_par,
        .fb_check_var = virtfb_check_var,
};

static int virtfb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
        return remap_vmalloc_range(vma, (void*)info->fix.smem_start, vma->vm_pgoff); 
}

static int virtfb_set_par(struct fb_info *info)
{
        struct fb_fix_screeninfo *fix = &info->fix;
        struct fb_var_screeninfo *var = &info->var;

        vfree((void*)fix->smem_start);

        fix->line_length = var->xres_virtual * var->bits_per_pixel / 8;
        fix->smem_len = fix->line_length * var->yres_virtual;

        fix->smem_start = (unsigned long)vmalloc_32_user(PAGE_ALIGN(fix->smem_len));
        if (fix->smem_start == 0) {
                pr_error("Failed to allocate memory");
                return -ENOMEM;
        }
        info->screen_base = (char __iomem *)fix->smem_start;
        info->screen_size = fix->smem_len;

        return 0;
}

static int virtfb_check_var(struct fb_var_screeninfo *var,
                struct fb_info *info)
{
        /*
         *  FB_VMODE_CONUPDATE and FB_VMODE_SMOOTH_XPAN are equal!
         *  as FB_VMODE_SMOOTH_XPAN is only used internally
         */

        if (var->vmode & FB_VMODE_CONUPDATE) {
                var->vmode |= FB_VMODE_YWRAP;
                var->xoffset = info->var.xoffset;
                var->yoffset = info->var.yoffset;
        }

        /*
         *  Some very basic checks
         */
        if (!var->xres)
                var->xres = 1;
        if (!var->yres)
                var->yres = 1;
        if (var->xres > var->xres_virtual)
                var->xres_virtual = var->xres;
        if (var->yres > var->yres_virtual)
                var->yres_virtual = var->yres;
        if (var->bits_per_pixel <= 1)
                var->bits_per_pixel = 1;
        else if (var->bits_per_pixel <= 8)
                var->bits_per_pixel = 8;
        else if (var->bits_per_pixel <= 16)
                var->bits_per_pixel = 16;
        else if (var->bits_per_pixel <= 24)
                var->bits_per_pixel = 24;
        else if (var->bits_per_pixel <= 32)
                var->bits_per_pixel = 32;
        else
                return -EINVAL;

        if (var->xres_virtual < var->xoffset + var->xres)
                var->xres_virtual = var->xoffset + var->xres;
        if (var->yres_virtual < var->yoffset + var->yres)
                var->yres_virtual = var->yoffset + var->yres;

        /*
         * Now that we checked it we alter var. The reason being is that the video
         * mode passed in might not work but slight changes to it might make it 
         * work. This way we let the user know what is acceptable.
         */
        switch (var->bits_per_pixel) {
                case 1:
                case 8:
                        var->red.offset = 0;
                        var->red.length = 8;
                        var->green.offset = 0;
                        var->green.length = 8;
                        var->blue.offset = 0;
                        var->blue.length = 8;
                        var->transp.offset = 0;
                        var->transp.length = 0;
                        break;
                case 16:		/* RGBA 5551 */
                        if (var->transp.length) {
                                var->red.offset = 0;
                                var->red.length = 5;
                                var->green.offset = 5;
                                var->green.length = 5;
                                var->blue.offset = 10;
                                var->blue.length = 5;
                                var->transp.offset = 15;
                                var->transp.length = 1;
                        } else {	/* RGB 565 */
                                var->red.offset = 0;
                                var->red.length = 5;
                                var->green.offset = 5;
                                var->green.length = 6;
                                var->blue.offset = 11;
                                var->blue.length = 5;
                                var->transp.offset = 0;
                                var->transp.length = 0;
                        }
                        break;
                case 24:		/* RGB 888 */
                        var->red.offset = 0;
                        var->red.length = 8;
                        var->green.offset = 8;
                        var->green.length = 8;
                        var->blue.offset = 16;
                        var->blue.length = 8;
                        var->transp.offset = 0;
                        var->transp.length = 0;
                        break;
                case 32:		/* RGBA 8888 */
                        var->red.offset = 0;
                        var->red.length = 8;
                        var->green.offset = 8;
                        var->green.length = 8;
                        var->blue.offset = 16;
                        var->blue.length = 8;
                        var->transp.offset = 24;
                        var->transp.length = 8;
                        break;
        }
        var->red.msb_right = 0;
        var->green.msb_right = 0;
        var->blue.msb_right = 0;
        var->transp.msb_right = 0;

        return 0;
}

static int virtfb_probe(struct platform_device *dev)
{
        struct fb_info *info;
        struct fb_videomode m;
        int ret = -ENOMEM;
        int memsize = 0;

        // Allocate framebuffer and register to platform device
        // Free cleanup
        info = framebuffer_alloc(sizeof(u32) * 256, &dev->dev);
        if (!info) goto end;

        // Set default var and fix
        sprintf(info->fix.id, "virtfb1");
        info->fbops = &virtfb_ops;
        info->var = default_var;
        info->fix = default_fix;

        if (!info->modelist.next || !info->modelist.prev)
                INIT_LIST_HEAD(&info->modelist);

        fb_var_to_videomode(&m, &info->var);
        fb_add_videomode(&m, &info->modelist);
        fb_set_var(info, &info->var);

        memsize = info->var.xres_virtual * info->var.yres_virtual * 
                info->var.bits_per_pixel / 8;
        info->fix.smem_start = (unsigned long)vmalloc_32_user(PAGE_ALIGN(memsize));
        ret = register_framebuffer(info);
        if (ret < 0) goto rel;

        platform_set_drvdata(dev, info);
        dev_info(&dev->dev, "Framebuffer registered\n");

        return 0;
rel:
        framebuffer_release(info);
end:
        return ret;
}

static int virtfb_remove(struct platform_device *dev)
{
        int ret = 0;
        struct fb_info *info = platform_get_drvdata(dev);
        if (info) {
                dev_info(&dev->dev, "Unregistering module\n");
                ret = unregister_framebuffer(info);
                if (ret < 0) pr_err("Unregister failed %d\n",ret);
                vfree((void*)info->fix.smem_start);
                framebuffer_release(info);

        }
        dev_info(&dev->dev, "Framebuffer released\n");
        return ret;
}

static struct platform_driver virtfb_driver = {
        .probe = virtfb_probe,
        .remove = virtfb_remove,
        .driver = {
                .name = DRIVER_NAME,
        },
};

static struct platform_device *virtfb_device;

static int __init virtfb_init(void)
{
        int ret = 0;

        // Register platform driver
        ret = platform_driver_register(&virtfb_driver);
        if (ret < 0) goto end;

        // Allocate platform device (Cleans up memory resources once released)
        virtfb_device = platform_device_alloc("virtfb", 0);
        if (!virtfb_device) {
                ret = -ENOMEM;
                goto unreg;
        }

        // Add platform device to device hierarchy
        ret = platform_device_add(virtfb_device);
        if (ret < 0) goto put;

        dev_info(&virtfb_device->dev, "VirtFB platform device registered\n");

        return ret;

put:
        platform_device_put(virtfb_device);
unreg:
        platform_driver_unregister(&virtfb_driver);
end:
        return ret;
}
module_init(virtfb_init);

static void __exit virtfb_exit(void)
{
        platform_device_unregister(virtfb_device);
        platform_driver_unregister(&virtfb_driver);

}
module_exit(virtfb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bram Vlerick <vlerickb@gmail.com>");
MODULE_SUPPORTED_DEVICE("fb");
