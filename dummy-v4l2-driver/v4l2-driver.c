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
#include <linux/platform_device.h>

MODULE_DESCRIPTION("Dummy v4l2 driver");
MODULE_AUTHOR("Bram Vlerick");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1");

#define DRIVER_NAME "v4l2_dummy"

struct dummy_v4l2_device {
        struct v4l2_device v4l2_dev;
        struct video_device *vfd;
        struct mutex mutex;
};

struct dummy_v4l2_device *ddev;

static int dummy_querycap(struct file *file, void *priv,
				struct v4l2_capability *cap)
{
	pr_info("Querying dummy device capabilities\n");
	
	return 0;
} 

static const struct v4l2_file_operations vd_ops = {
	.owner = THIS_MODULE,
	.open = v4l2_fh_open,
};

static const struct v4l2_ioctl_ops vd_ioctl_ops = {
	.vidioc_querycap = dummy_querycap,
};

static struct video_device dd_template = {
        .name		= "dummy dev",
        .fops       	= &vd_ops,
        .ioctl_ops 	= &vd_ioctl_ops,
        .release	= video_device_release_empty,
};

static int v4l2_dummy_probe(struct platform_device *dev)
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

	pr_info("V4L2 device registered\n");

        ddev->vfd = video_device_alloc();
        if (!ddev->vfd) goto ddev_unreg;

	pr_info("Video device allocated\n");
	
        *ddev->vfd = dd_template;
        ddev->vfd->v4l2_dev = &ddev->v4l2_dev;
        ddev->vfd->lock = &ddev->mutex;
	ddev->vfd->device_caps = V4L2_CAP_STREAMING;

	pr_info("Template setup complete\n");

        video_set_drvdata(ddev->vfd, ddev);
        
	res = video_register_device(ddev->vfd, VFL_TYPE_GRABBER, -1);
        if (res < 0) goto ddev_rel;

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

static int v4l2_dummy_remove(struct platform_device *dev)
{
        pr_info("Removing v4l2 driver\n");
        video_unregister_device(ddev->vfd);
        v4l2_device_unregister(&ddev->v4l2_dev);
        kfree(ddev);
	return 0;
}

static struct platform_driver v4l2_dummy_driver = {
        .probe = v4l2_dummy_probe,
        .remove = v4l2_dummy_remove,
        .driver = {
                .name = DRIVER_NAME,
        },
};

static struct platform_device *v4l2_dummy_device;

static int __init v4l2_dummy_init(void)
{
        int ret = 0;

        // Register platform driver
        ret = platform_driver_register(&v4l2_dummy_driver);
        if (ret < 0) goto end;

        // Allocate platform device (Cleans up memory resources once released)
        v4l2_dummy_device = platform_device_alloc("v4l2_dummy", 0);
        if (!v4l2_dummy_device) {
                ret = -ENOMEM;
                goto unreg;
        }

        // Add platform device to device hierarchy
        ret = platform_device_add(v4l2_dummy_device);
        if (ret < 0) goto put;

        dev_info(&v4l2_dummy_device->dev, "Dummy V4L2 platform device registered\n");

        return ret;

put:
        platform_device_put(v4l2_dummy_device);
unreg:
        platform_driver_unregister(&v4l2_dummy_driver);
end:
        return ret;
}

static void __exit v4l2_dummy_exit(void)
{
        platform_device_unregister(v4l2_dummy_device);
        platform_driver_unregister(&v4l2_dummy_driver);

}

module_init(v4l2_dummy_init);
module_exit(v4l2_dummy_exit);
