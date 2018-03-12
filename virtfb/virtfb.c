#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/vmalloc.h>

#define DRIVER_NAME "virtfb"

//
static struct fb_info *virtfb;
static void *videomemory;

static int virtfb_mmap(struct fb_info* info, struct vm_area_struct *vma)
{
        return remap_vmalloc_range(vma, (void*)virtfb->fix.smem_start, vma->vm_pgoff); 
}

static int virtfb_map_video_memory(struct fb_info *fbi)
{
        if (fbi->fix.smem_len < fbi->var.yres_virtual * fbi->fix.line_length)
                fbi->fix.smem_len = fbi->var.yres_virtual *
                        fbi->fix.line_length;

        videomemory = vmalloc_32_user(fbi->fix.smem_len);

        fbi->screen_base = (char __iomem *)videomemory;
        fbi->fix.smem_start = (unsigned long)videomemory;

        if (fbi->fix.smem_start == 0) {
                dev_err(fbi->device, "Unable to allocate framebuffer memory\n");
                fbi->fix.smem_len = 0;
                return -EBUSY;
        }

        pr_info("allocated fb @ paddr=0x%08X, size=%d.\n",
                        (uint32_t) fbi->fix.smem_start, fbi->fix.smem_len);

        fbi->screen_size = fbi->fix.smem_len;

        return 0;
}

static int virtfb_unmap_video_memory(struct fb_info *fbi)
{
        vfree(videomemory);

        fbi->screen_base = 0;
	fbi->fix.smem_start = 0;
	fbi->fix.smem_len = 0;
	return 0;
}

static int virtfb_set_par(struct fb_info *info)
{
        struct fb_fix_screeninfo *fix = &info->fix;
	struct fb_var_screeninfo *var = &info->var;
        u32 mem_len;

        pr_info("Setting video mode\n");

	fix->line_length = var->xres_virtual * var->bits_per_pixel / 8;

	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->accel = FB_ACCEL_NONE;
	fix->visual = FB_VISUAL_TRUECOLOR;
	fix->xpanstep = 1;
	fix->ywrapstep = 1;
	fix->ypanstep = 1;

        mem_len = info->var.yres_virtual * info->fix.line_length;
	if (!info->fix.smem_start || (mem_len > info->fix.smem_len)) {
		if (info->fix.smem_start)
			virtfb_unmap_video_memory(info);

		if (virtfb_map_video_memory(info) < 0)
			return -ENOMEM;
	}

        return 0;
}

static int virtfb_check_var(struct fb_var_screeninfo *var,
                struct fb_info *info)
{
        pr_info("Checking var\n");
        if (var->xres_virtual < var->xres) {
                var->xres_virtual = var->xres;
        }
        if (var->yres_virtual < var->yres) {
                var->yres_virtual = var->yres;
        }
        
        virtfb->var.bits_per_pixel = 24;

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


        return 0;
}

static struct fb_ops virtfb_ops = {
        .owner = THIS_MODULE,
        .fb_set_par = virtfb_set_par,
        .fb_check_var = virtfb_check_var,
        .fb_mmap = virtfb_mmap,
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
        virtfb->var.bits_per_pixel = 24;

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
MODULE_SUPPORTED_DEVICE("fb");

module_init(virtfb_init);
module_exit(virtfb_exit);
