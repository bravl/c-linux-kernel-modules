#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/font.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/kthread.h>
#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-event.h>
#include <media/v4l2-common.h>

MODULE_DESCRIPTION("Dummy v4l2 driver");
MODULE_AUTHOR("Bram Vlerick");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

struct dummy_v4l2_device {
        struct v4l2_device v4l2_dev;
        struct video_device *vfd;
        struct mutex mutex;
};

static struct video_device dd_template = {
        .name		= "dummy dev",
        .fops       = NULL,
        .ioctl_ops 	= NULL,
        .release	= video_device_release,
};

struct dummy_v4l2_device *ddev;

static int __init v4l2_dummy_init(void)
{
        int res = 0;
        pr_info("Running v4l2 dummy driver\n");

        ddev = kzalloc(sizeof(*ddev), GFP_KERNEL);
        if (!ddev) {
                pr_err("Failed to allocat ddev");
                return -ENOMEM;
        }

        mutex_init(&ddev->mutex);

        snprintf(ddev->v4l2_dev.name, sizeof(ddev->v4l2_dev.name),
                 "%s", "Dummy driver");
        res = v4l2_device_register(NULL, &ddev->v4l2_dev);
        if (res) goto ddev_free;

        ddev->vfd = video_device_alloc();
        if (!ddev->vfd) goto ddev_unreg;

        *ddev->vfd = dd_template;
        ddev->vfd->v4l2_dev = &ddev->v4l2_dev;
        ddev->vfd->lock = &ddev->mutex;

        res = video_register_device(ddev->vfd, VFL_TYPE_GRABBER, -1);
        if (res < 0) goto ddev_rel;

        //video_set_drvdata(ddev->vfd, ddev);

        v4l2_info(&ddev->v4l2_dev, "Registered V4L2 device as %s\n",
                  video_device_node_name(ddev->vfd));

        return res;

 ddev_rel:
        video_device_release(ddev->vfd);

 ddev_unreg:
        v4l2_device_unregister(&ddev->v4l2_dev);

 ddev_free:
        kfree(ddev);

        return res;
}

static void __exit v4l2_dummy_exit(void)
{
        pr_info("Removing v4l2 driver\n");
        video_unregister_device(ddev->vfd);
        v4l2_device_unregister(&ddev->v4l2_dev);
        kfree(ddev);
}

module_init(v4l2_dummy_init);
module_exit(v4l2_dummy_exit);
