#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>

#define DRIVER_NAME "alsa-gpio"

struct alsa_gpio_timer_ops {
	int (*create)(struct snd_pcm_substream *);
	void (*free)(struct snd_pcm_substream *);
	int (*prepare)(struct snd_pcm_substream *);
	int (*start)(struct snd_pcm_substream *);
	int (*stop)(struct snd_pcm_substream *);
	snd_pcm_uframes_t (*pointer)(struct snd_pcm_substream *);
};

#define get_alsa_gpio_ops(substream) \
	(*(const struct alsa_gpio_timer_ops **)(substream)->runtime->private_data)

struct alsa_gpio_model {
	const char *name;
	int (*playback_constraints)(struct snd_pcm_runtime *runtime);
	int (*capture_constraints)(struct snd_pcm_runtime *runtime);
	u64 formats;
	size_t buffer_bytes_max;
	size_t period_bytes_min;
	size_t period_bytes_max;
	unsigned int periods_min;
	unsigned int periods_max;
	unsigned int rates;
	unsigned int rate_min;
	unsigned int rate_max;
	unsigned int channels_min;
	unsigned int channels_max;
};

static struct alsa_gpio_model model_gpio = {
	.name = "alsa_gpio",
	.formats = SNDRV_PCM_FMTBIT_S8,
	.channels_min = 2,
	.channels_max = 2,
	.rates = SNDRV_PCM_RATE_8000,
	.rate_min = 8000,
	.rate_max = 8000,
};


struct snd_alsa_gpio {
	struct snd_card *card;
	struct alsa_gpio_model *model;
	struct snd_pcm *pcm;
	struct snd_pcm_hardware pcm_hw;
	spinlock_t mixer_lock;
	int mixer_volume[2];
	int capture_source[2];
	int iobox;
	struct snd_kcontrol *cd_volume_ctl;
	struct snd_kcontrol *cd_switch_ctl;
};

static int alsa_gpio_probe(struct platform_device *dev)
{
        struct snd_card *card;
        struct snd_alsa_gpio *alsa_gpio;
        struct alsa_gpio_model *m = NULL;
        int err;

        pr_info("Probing sound driver\n");
        err = snd_card_new(&dev->dev, 0, 0, THIS_MODULE,
                        sizeof(struct snd_alsa_gpio), &card);
        if (err < 0) {
                pr_err("Failed to create sound card\n");
                return err;
        }

        alsa_gpio = card->private_data;
        alsa_gpio->card = card;
        m = alsa_gpio->model = &model_gpio;

        strcpy(card->driver, "Alsa-GPIO");
	strcpy(card->shortname, "Alsa-GPIO");
	sprintf(card->longname, "Alsa-GPIO %i", 0);



        err = snd_card_register(card);
        if (err == 0) {
                platform_set_drvdata(dev,card);
                return 0;
        }

error:
        snd_card_free(card);
        return err;
}

static int alsa_gpio_remove(struct platform_device *dev)
{
        pr_info("Removing sound driver\n");
        snd_card_free(platform_get_drvdata(dev));
        return 0;
}

static struct platform_driver alsa_gpio_driver = {
        .probe = alsa_gpio_probe,
        .remove = alsa_gpio_remove,
        .driver = {
                .name = DRIVER_NAME,
        },
};

static struct platform_device *alsa_gpio_device;

static int __init alsa_gpio_init(void)
{
        int ret = 0;

        // Register platform driver
        ret = platform_driver_register(&alsa_gpio_driver);
        if (ret < 0) goto end;

        // Allocate platform device (Cleans up memory resources once released)
        alsa_gpio_device = platform_device_alloc(DRIVER_NAME, 0);
        if (!alsa_gpio_device) {
                ret = -ENOMEM;
                goto unreg;
        }

        // Add platform device to device hierarchy
        ret = platform_device_add(alsa_gpio_device);
        if (ret < 0) goto put;

        dev_info(&alsa_gpio_device->dev, "alsa_gpio platform device registered\n");

        return ret;

put:
        platform_device_put(alsa_gpio_device);
unreg:
        platform_driver_unregister(&alsa_gpio_driver);
end:
        return ret;
}
module_init(alsa_gpio_init);

static void __exit alsa_gpio_exit(void)
{
        platform_device_unregister(alsa_gpio_device);
        platform_driver_unregister(&alsa_gpio_driver);

}
module_exit(alsa_gpio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Bram Vlerick <vlerickb@gmail.com>");
