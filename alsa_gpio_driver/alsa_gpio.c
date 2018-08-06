#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>

#include "music.h"

#define DRIVER_NAME "alsa-gpio"

#define USE_RATE_MIN            8000
#define USE_RATE_MAX            8000
#define USE_CHANNELS_MIN        2
#define USE_CHANNELS_MAX        2
#define USE_FORMATS             SNDRV_PCM_FMTBIT_U8
#define USE_RATE                SNDRV_PCM_RATE_8000
#define USE_PERIODS_MIN         1
#define USE_PERIODS_MAX         1024
#define MAX_BUFFER_SIZE		(64*1024)
#define MIN_PERIOD_SIZE		64
#define MAX_PERIOD_SIZE		MAX_BUFFER_SIZE

static int pcm_substream = 1;
static int pcm_dev = 1;
static int buffer_counter = 0;

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
        .formats = SNDRV_PCM_FMTBIT_U8,
        .channels_min = USE_CHANNELS_MIN,
        .channels_max = USE_CHANNELS_MAX,
        .rates = SNDRV_PCM_RATE_8000,
        .rate_min = USE_RATE_MIN,
        .rate_max = USE_RATE_MAX,
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



/*
 * system timer interface
 */

struct alsa_gpio_systimer_pcm {
        /* ops must be the first item */
        const struct alsa_gpio_timer_ops *timer_ops;
        spinlock_t lock;
        struct timer_list timer;
        unsigned long base_time;
        unsigned int frac_pos;	/* fractional sample position (based HZ) */
        unsigned int frac_period_rest;
        unsigned int frac_buffer_size;	/* buffer_size * HZ */
        unsigned int frac_period_size;	/* period_size * HZ */
        unsigned int rate;
        int elapsed;
        struct snd_pcm_substream *substream;
};

static void alsa_gpio_systimer_rearm(struct alsa_gpio_systimer_pcm *dpcm)
{
        mod_timer(&dpcm->timer, jiffies +
                        (dpcm->frac_period_rest + dpcm->rate - 1) / dpcm->rate);
}

static void alsa_gpio_systimer_update(struct alsa_gpio_systimer_pcm *dpcm)
{
        unsigned long delta;

        delta = jiffies - dpcm->base_time;
        if (!delta)
                return;
        dpcm->base_time += delta;
        delta *= dpcm->rate;
        dpcm->frac_pos += delta;
        while (dpcm->frac_pos >= dpcm->frac_buffer_size)
                dpcm->frac_pos -= dpcm->frac_buffer_size;
        while (dpcm->frac_period_rest <= delta) {
                dpcm->elapsed++;
                dpcm->frac_period_rest += dpcm->frac_period_size;
        }
        dpcm->frac_period_rest -= delta;
}

static int alsa_gpio_systimer_start(struct snd_pcm_substream *substream)
{
        struct alsa_gpio_systimer_pcm *dpcm = substream->runtime->private_data;
        spin_lock(&dpcm->lock);
        buffer_counter = 0;
        dpcm->base_time = jiffies;
        alsa_gpio_systimer_rearm(dpcm);
        spin_unlock(&dpcm->lock);
        return 0;
}

static int alsa_gpio_systimer_stop(struct snd_pcm_substream *substream)
{
        struct alsa_gpio_systimer_pcm *dpcm = substream->runtime->private_data;
        spin_lock(&dpcm->lock);
        del_timer(&dpcm->timer);
        spin_unlock(&dpcm->lock);
        return 0;
}

static int alsa_gpio_systimer_prepare(struct snd_pcm_substream *substream)
{
        struct snd_pcm_runtime *runtime = substream->runtime;
        struct alsa_gpio_systimer_pcm *dpcm = runtime->private_data;

        dpcm->frac_pos = 0;
        dpcm->rate = runtime->rate;
        dpcm->frac_buffer_size = runtime->buffer_size * HZ;
        dpcm->frac_period_size = runtime->period_size * HZ;
        dpcm->frac_period_rest = dpcm->frac_period_size;
        dpcm->elapsed = 0;

        return 0;
}

static void alsa_gpio_systimer_callback(struct timer_list *t)
{
        int tmp_bufsize = 0;
        pr_info("Systimer callback\n");
        struct alsa_gpio_systimer_pcm *dpcm = from_timer(dpcm, t, timer);
        unsigned long flags;
        int elapsed = 0;

        spin_lock_irqsave(&dpcm->lock, flags);
        alsa_gpio_systimer_update(dpcm);
        alsa_gpio_systimer_rearm(dpcm);
        elapsed = dpcm->elapsed;
        dpcm->elapsed = 0;
        
        tmp_bufsize = dpcm->substream->runtime->dma_bytes / 2;

        memcpy(dpcm->substream->runtime->dma_area, &music[tmp_bufsize * buffer_counter], 
                        dpcm->substream->runtime->dma_bytes);
        
        if ((tmp_bufsize * buffer_counter) == 48000)
                buffer_counter = 0;
        else
                buffer_counter++;

        spin_unlock_irqrestore(&dpcm->lock, flags);
        if (elapsed)
                snd_pcm_period_elapsed(dpcm->substream);
}

static snd_pcm_uframes_t alsa_gpio_systimer_pointer(struct 
                snd_pcm_substream *substream)
{
        struct alsa_gpio_systimer_pcm *dpcm = substream->runtime->private_data;
        snd_pcm_uframes_t pos;

        spin_lock(&dpcm->lock);
        alsa_gpio_systimer_update(dpcm);
        pos = dpcm->frac_pos / HZ;
        pr_info("Pointer: %ld",pos);
        spin_unlock(&dpcm->lock);
        return pos;
}

static int alsa_gpio_systimer_create(struct snd_pcm_substream *substream)
{
        struct alsa_gpio_systimer_pcm *dpcm;

        dpcm = kzalloc(sizeof(*dpcm), GFP_KERNEL);
        if (!dpcm)
                return -ENOMEM;
        substream->runtime->private_data = dpcm;
        timer_setup(&dpcm->timer, alsa_gpio_systimer_callback, 0);
        spin_lock_init(&dpcm->lock);
        dpcm->substream = substream;
        return 0;
}

static void alsa_gpio_systimer_free(struct snd_pcm_substream *substream)
{
        kfree(substream->runtime->private_data);
}

static const struct alsa_gpio_timer_ops alsa_gpio_systimer_ops = {
        .create =	alsa_gpio_systimer_create,
        .free =		alsa_gpio_systimer_free,
        .prepare =	alsa_gpio_systimer_prepare,
        .start =	alsa_gpio_systimer_start,
        .stop =		alsa_gpio_systimer_stop,
        .pointer =	alsa_gpio_systimer_pointer,
};

/*
 * PCM interface
 */

static int alsa_gpio_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
        pr_info("PCM triggerd\n");
        switch (cmd) {
                case SNDRV_PCM_TRIGGER_START:
                case SNDRV_PCM_TRIGGER_RESUME:
                        return get_alsa_gpio_ops(substream)->start(substream);
                case SNDRV_PCM_TRIGGER_STOP:
                case SNDRV_PCM_TRIGGER_SUSPEND:
                        pr_info("Stopping timer\n");
                        return get_alsa_gpio_ops(substream)->stop(substream);
        }
        return -EINVAL;
}

static int alsa_gpio_pcm_prepare(struct snd_pcm_substream *substream)
{
        return get_alsa_gpio_ops(substream)->prepare(substream);
}

static snd_pcm_uframes_t alsa_gpio_pcm_pointer(struct snd_pcm_substream *substream)
{
        return get_alsa_gpio_ops(substream)->pointer(substream);
}

static const struct snd_pcm_hardware alsa_gpio_pcm_hardware = {
        .info =			(SNDRV_PCM_INFO_MMAP |
                        SNDRV_PCM_INFO_INTERLEAVED |
                        SNDRV_PCM_INFO_RESUME |
                        SNDRV_PCM_INFO_MMAP_VALID),
        .formats =		USE_FORMATS,
        .rates =		USE_RATE,
        .rate_min =		USE_RATE_MIN,
        .rate_max =		USE_RATE_MAX,
        .channels_min =		USE_CHANNELS_MIN,
        .channels_max =		USE_CHANNELS_MAX,
        .buffer_bytes_max =	MAX_BUFFER_SIZE,
        .period_bytes_min =	MIN_PERIOD_SIZE,
        .period_bytes_max =	MAX_PERIOD_SIZE,
        .periods_min =		USE_PERIODS_MIN,
        .periods_max =		USE_PERIODS_MAX,
        .fifo_size =		0,
};

static int alsa_gpio_pcm_hw_params(struct snd_pcm_substream *substream,
                struct snd_pcm_hw_params *hw_params)
{
        return snd_pcm_lib_malloc_pages(substream,
                        params_buffer_bytes(hw_params));
}

static int alsa_gpio_pcm_hw_free(struct snd_pcm_substream *substream)
{
        return snd_pcm_lib_free_pages(substream);
}

static int alsa_gpio_pcm_open(struct snd_pcm_substream *substream)
{
        struct snd_alsa_gpio *alsa_gpio = snd_pcm_substream_chip(substream);
        struct alsa_gpio_model *model = alsa_gpio->model;
        struct snd_pcm_runtime *runtime = substream->runtime;
        const struct alsa_gpio_timer_ops *ops;
        int err;

        ops = &alsa_gpio_systimer_ops;
        err = ops->create(substream);
        if (err < 0)
                return err;
        get_alsa_gpio_ops(substream) = ops;

        runtime->hw = alsa_gpio->pcm_hw;
        if (substream->pcm->device & 1) {
                runtime->hw.info &= ~SNDRV_PCM_INFO_INTERLEAVED;
                runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;
        }
        if (substream->pcm->device & 2)
                runtime->hw.info &= ~(SNDRV_PCM_INFO_MMAP |
                                SNDRV_PCM_INFO_MMAP_VALID);

        if (model == NULL)
                return 0;

        if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
                if (model->playback_constraints)
                        err = model->playback_constraints(substream->runtime);
        } else {
                if (model->capture_constraints)
                        err = model->capture_constraints(substream->runtime);
        }
        if (err < 0) {
                get_alsa_gpio_ops(substream)->free(substream);
                return err;
        }
        return 0;
}

static int alsa_gpio_pcm_close(struct snd_pcm_substream *substream)
{
        get_alsa_gpio_ops(substream)->free(substream);
        return 0;
}

/*
 * alsa_gpio buffer handling
 */

static struct snd_pcm_ops alsa_gpio_pcm_ops = {
        .open =		alsa_gpio_pcm_open,
        .close =	alsa_gpio_pcm_close,
        .ioctl =	snd_pcm_lib_ioctl,
        .hw_params =	alsa_gpio_pcm_hw_params,
        .hw_free =	alsa_gpio_pcm_hw_free,
        .prepare =	alsa_gpio_pcm_prepare,
        .trigger =	alsa_gpio_pcm_trigger,
        .pointer =	alsa_gpio_pcm_pointer,
};


static int snd_card_alsa_gpio_pcm(struct snd_alsa_gpio *alsa_gpio, int device,
                int substreams)
{
        struct snd_pcm *pcm;
        struct snd_pcm_ops *ops;
        int err;

        err = snd_pcm_new(alsa_gpio->card, "alsa_gpio PCM", device,
                        substreams, substreams, &pcm);
        if (err < 0)
                return err;
        alsa_gpio->pcm = pcm;
        ops = &alsa_gpio_pcm_ops;
        snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, ops);
        snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, ops);
        pcm->private_data = alsa_gpio;
        pcm->info_flags = 0;
        strcpy(pcm->name, "alsa_gpio PCM");

        snd_pcm_lib_preallocate_pages_for_all(pcm,
                        SNDRV_DMA_TYPE_CONTINUOUS,
                        snd_dma_continuous_data(GFP_KERNEL),
                        0, 64*1024);

        return 0;
}


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

        snd_card_alsa_gpio_pcm(alsa_gpio, 0, pcm_substream);
        if (err < 0)
                goto error;

        alsa_gpio->pcm_hw = alsa_gpio_pcm_hardware;
        if (m) {
                if (m->formats)
                        alsa_gpio->pcm_hw.formats = m->formats;
                if (m->buffer_bytes_max)
                        alsa_gpio->pcm_hw.buffer_bytes_max = 
                                m->buffer_bytes_max;
                if (m->period_bytes_min)
                        alsa_gpio->pcm_hw.period_bytes_min = 
                                m->period_bytes_min;
                if (m->period_bytes_max)
                        alsa_gpio->pcm_hw.period_bytes_max = 
                                m->period_bytes_max;
                if (m->periods_min)
                        alsa_gpio->pcm_hw.periods_min = m->periods_min;
                if (m->periods_max)
                        alsa_gpio->pcm_hw.periods_max = m->periods_max;
                if (m->rates)
                        alsa_gpio->pcm_hw.rates = m->rates;
                if (m->rate_min)
                        alsa_gpio->pcm_hw.rate_min = m->rate_min;
                if (m->rate_max)
                        alsa_gpio->pcm_hw.rate_max = m->rate_max;
                if (m->channels_min)
                        alsa_gpio->pcm_hw.channels_min = m->channels_min;
                if (m->channels_max)
                        alsa_gpio->pcm_hw.channels_max = m->channels_max;
        }

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
