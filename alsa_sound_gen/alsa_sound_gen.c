/*
 * Alsa Sound Generator Driver 
 *  
 * Copyright Bram Vlerick <vlerickb@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>

#include "music_gen.h"

MODULE_AUTHOR("Bram Vlerick <vlerickb@gmail.com>");
MODULE_DESCRIPTION("PCM Sound generator");
MODULE_LICENSE("GPL");
MODULE_SUPPORTED_DEVICE("{{ALSA,PCM Sound generator}}");

static struct platform_device *device;


struct snd_card_soundgen {
        struct snd_card *card;
        struct snd_pcm *pcm;
        struct snd_pcm_hardware pcm_hw;
};

/**
 * Timer stuff
 */

struct snd_timer_ops {
        int (*create)(struct snd_pcm_substream *);
        void (*free)(struct snd_pcm_substream *);
        int (*prepare)(struct snd_pcm_substream *);
        int (*start)(struct snd_pcm_substream *);
        int (*stop)(struct snd_pcm_substream *);
        snd_pcm_uframes_t (*pointer)(struct snd_pcm_substream *);
};


/*
 * hrtimer interface
 */

struct dummy_hrtimer_pcm {
	/* ops must be the first item */
	const struct snd_timer_ops *timer_ops;
	ktime_t base_time;
	ktime_t period_time;
	atomic_t running;
	struct hrtimer timer;
	struct snd_pcm_substream *substream;
};

static enum hrtimer_restart dummy_hrtimer_callback(struct hrtimer *timer)
{
	struct dummy_hrtimer_pcm *dpcm;
        u64 delta;
	u32 pos;
        struct snd_pcm_runtime *runtime;

	dpcm = container_of(timer, struct dummy_hrtimer_pcm, timer);
	if (!atomic_read(&dpcm->running))
		return HRTIMER_NORESTART;
        runtime = dpcm->substream->runtime;
	/*
	 * In cases of XRUN and draining, this calls .trigger to stop PCM
	 * substream.
	 */
	delta = ktime_us_delta(hrtimer_cb_get_time(&dpcm->timer),
			       dpcm->base_time);
	delta = div_u64(delta * runtime->rate + 999999, 1000000);
	div_u64_rem(delta, runtime->buffer_size, &pos);
        
        snd_pcm_period_elapsed(dpcm->substream);
	if (!atomic_read(&dpcm->running))
		return HRTIMER_NORESTART;

	hrtimer_forward_now(timer, dpcm->period_time);
	return HRTIMER_RESTART;
}

static int dummy_hrtimer_start(struct snd_pcm_substream *substream)
{
	struct dummy_hrtimer_pcm *dpcm = substream->runtime->private_data;
        struct snd_pcm_runtime *runtime = substream->runtime;
        memset(runtime->dma_area,0x99,runtime->dma_bytes); 
	dpcm->base_time = hrtimer_cb_get_time(&dpcm->timer);
	hrtimer_start(&dpcm->timer, dpcm->period_time, HRTIMER_MODE_REL_SOFT);
	atomic_set(&dpcm->running, 1);
	return 0;
}

static int dummy_hrtimer_stop(struct snd_pcm_substream *substream)
{
	struct dummy_hrtimer_pcm *dpcm = substream->runtime->private_data;

	atomic_set(&dpcm->running, 0);
	if (!hrtimer_callback_running(&dpcm->timer))
		hrtimer_cancel(&dpcm->timer);
	return 0;
}

static inline void dummy_hrtimer_sync(struct dummy_hrtimer_pcm *dpcm)
{
	hrtimer_cancel(&dpcm->timer);
}

static snd_pcm_uframes_t
dummy_hrtimer_pointer(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dummy_hrtimer_pcm *dpcm = runtime->private_data;
	u64 delta;
	u32 pos;

	delta = ktime_us_delta(hrtimer_cb_get_time(&dpcm->timer),
			       dpcm->base_time);
	delta = div_u64(delta * runtime->rate + 999999, 1000000);
	div_u64_rem(delta, runtime->buffer_size, &pos);
        pr_debug("Position: %d\n",pos);
	return pos;
}

static int dummy_hrtimer_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct dummy_hrtimer_pcm *dpcm = runtime->private_data;
	unsigned int period, rate;
	long sec;
	unsigned long nsecs;

	dummy_hrtimer_sync(dpcm);
	period = runtime->period_size;
	rate = runtime->rate;
	sec = period / rate;
	period %= rate;
	nsecs = div_u64((u64)period * 1000000000UL + rate - 1, rate);
	dpcm->period_time = ktime_set(sec, nsecs);

	return 0;
}

static int dummy_hrtimer_create(struct snd_pcm_substream *substream)
{
	struct dummy_hrtimer_pcm *dpcm;

	dpcm = kzalloc(sizeof(*dpcm), GFP_KERNEL);
	if (!dpcm)
		return -ENOMEM;
	substream->runtime->private_data = dpcm;
	hrtimer_init(&dpcm->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL_SOFT);
	dpcm->timer.function = dummy_hrtimer_callback;
	dpcm->substream = substream;
	atomic_set(&dpcm->running, 0);
	return 0;
}

static void dummy_hrtimer_free(struct snd_pcm_substream *substream)
{
	struct dummy_hrtimer_pcm *dpcm = substream->runtime->private_data;
	dummy_hrtimer_sync(dpcm);
	kfree(dpcm);
}

static const struct snd_timer_ops dummy_hrtimer_ops = {
	.create =	dummy_hrtimer_create,
	.free =		dummy_hrtimer_free,
	.prepare =	dummy_hrtimer_prepare,
	.start =	dummy_hrtimer_start,
	.stop =		dummy_hrtimer_stop,
	.pointer =	dummy_hrtimer_pointer,
};

#define get_timer_ops(substream) \
(*(const struct snd_timer_ops **)(substream)->runtime->private_data)

struct snd_pcm_timer {
        /* ops must be the first item */
        const struct snd_timer_ops *timer_ops;
        spinlock_t lock;
        struct timer_list timer;
        unsigned long base_time;
        unsigned int frac_pos;	/* fractional sample position (based HZ) */
        unsigned int frac_period_rest;
        unsigned int frac_buffer_size;	/* buffer_size * HZ */
        unsigned int frac_period_size;	/* period_size * HZ */
        unsigned int rate;
        long int elapsed;
        struct snd_pcm_substream *substream;
};

static void snd_pcm_timer_rearm(struct snd_pcm_timer *dpcm)
{
        pr_debug("rearming with %d ms\n",
                        (dpcm->frac_period_rest + dpcm->rate - 1) / dpcm->rate);
        mod_timer(&dpcm->timer, jiffies +
                        (dpcm->frac_period_rest + dpcm->rate - 1) / dpcm->rate);
}

static void snd_pcm_timer_update(struct snd_pcm_timer *dpcm)
{
        unsigned long delta;

        delta = jiffies - dpcm->base_time;
        if (!delta)
                return;
        dpcm->base_time += delta;
        delta *= dpcm->rate;
        dpcm->frac_pos += delta;
        while (dpcm->frac_pos >= dpcm->frac_buffer_size) {
                dpcm->frac_pos -= dpcm->frac_buffer_size;
        }
        while (dpcm->frac_period_rest <= delta) {
                dpcm->elapsed++;
                dpcm->frac_period_rest += dpcm->frac_period_size;
        }
        dpcm->frac_period_rest -= delta;
}

static int snd_pcm_timer_start(struct snd_pcm_substream *substream)
{
        struct snd_pcm_timer *dpcm = substream->runtime->private_data;
        spin_lock(&dpcm->lock);
        dpcm->base_time = jiffies;
        snd_pcm_timer_rearm(dpcm);
        spin_unlock(&dpcm->lock);
        return 0;
}

static int snd_pcm_timer_stop(struct snd_pcm_substream *substream)
{ 
        struct snd_pcm_timer *dpcm = substream->runtime->private_data;
        spin_lock(&dpcm->lock);
        del_timer(&dpcm->timer);
        spin_unlock(&dpcm->lock);
        return 0;
}

static int snd_pcm_timer_prepare(struct snd_pcm_substream *substream)
{
        struct snd_pcm_runtime *runtime = substream->runtime;
        struct snd_pcm_timer *dpcm = runtime->private_data;

        dpcm->frac_pos = 0;
        dpcm->rate = runtime->rate;
        dpcm->frac_buffer_size = runtime->buffer_size * HZ;
        dpcm->frac_period_size = runtime->period_size * HZ;
        dpcm->frac_period_rest = dpcm->frac_period_size;
        dpcm->elapsed = 0;

        pr_debug("%d\n", dpcm->frac_pos); 
        pr_debug("%d\n", dpcm->rate); 
        pr_debug("%d\n", dpcm->frac_buffer_size); 
        pr_debug("%d\n", dpcm->frac_period_size); 
        pr_debug("%d\n", dpcm->frac_period_rest); 
        pr_debug("%ld\n", dpcm->elapsed); 

        return 0;
}

static void snd_pcm_timer_callback(struct timer_list *t)
{
        pr_info("Systimer callback\n");
        struct snd_pcm_timer *dpcm = from_timer(dpcm, t, timer);
        unsigned long flags;
        int elapsed = 0;

        spin_lock_irqsave(&dpcm->lock, flags);
        snd_pcm_timer_update(dpcm);
        snd_pcm_timer_rearm(dpcm);
        elapsed = dpcm->elapsed;
        dpcm->elapsed = 0;
        pr_debug("elapsed = %d\n",elapsed);
       
        // This needs an algorithm to check the current position,
        // update the offset and copy data to the correct offset.
        memcpy(dpcm->substream->runtime->dma_area,
               music, dpcm->substream->runtime->dma_bytes);

        spin_unlock_irqrestore(&dpcm->lock, flags);
        if (elapsed)
                snd_pcm_period_elapsed(dpcm->substream);
}

static snd_pcm_uframes_t snd_pcm_timer_pointer(struct 
                snd_pcm_substream *substream)
{
        struct snd_pcm_timer *dpcm = substream->runtime->private_data;
        snd_pcm_uframes_t pos;

        spin_lock(&dpcm->lock);
        snd_pcm_timer_update(dpcm);
        pos = dpcm->frac_pos / HZ;
        pr_debug("Pointer pos %ld\n",pos);
        spin_unlock(&dpcm->lock);
        return pos;
}

static int snd_pcm_timer_create(struct snd_pcm_substream *substream)
{
        struct snd_pcm_timer *dpcm;

        dpcm = kzalloc(sizeof(*dpcm), GFP_KERNEL);
        if (!dpcm)
                return -ENOMEM;
        substream->runtime->private_data = dpcm;
        timer_setup(&dpcm->timer, snd_pcm_timer_callback, 0);
        spin_lock_init(&dpcm->lock);
        dpcm->substream = substream;
        return 0;
}

static void snd_pcm_timer_free(struct snd_pcm_substream *substream)
{
        kfree(substream->runtime->private_data);
}

static const struct snd_timer_ops snd_pcm_timer_ops = {
        .create = snd_pcm_timer_create,
        .free = snd_pcm_timer_free,
        .prepare = snd_pcm_timer_prepare,
        .start = snd_pcm_timer_start,
        .stop = snd_pcm_timer_stop,
        .pointer = snd_pcm_timer_pointer,
};


/**
 * End of timer stuff
 */

static const struct snd_pcm_hardware snd_soundgen_hw = {
        .info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
                        SNDRV_PCM_INFO_BLOCK_TRANSFER | 
                        SNDRV_PCM_INFO_MMAP_VALID),
        .formats = SNDRV_PCM_FMTBIT_S8,
        .rates = SNDRV_PCM_RATE_8000,
        .rate_min = 8000,
        .rate_max = 8000,
        .channels_min = 1,
        .channels_max = 1,
        .buffer_bytes_max = 32768,
        .period_bytes_min = 64,
        .period_bytes_max = 32768,
        .periods_min = 1,
        .periods_max = 1024,
        .fifo_size = 0,
};

/**
 * PCM Stuff
 */

static int soundgen_pcm_open(struct snd_pcm_substream *substream)
{
        int err;
        struct snd_card_soundgen *chip = snd_pcm_substream_chip(substream);
        if (!chip) {
                pr_info("Failed to retrieve chip\n");
                return -1;
        }
        struct snd_pcm_runtime *runtime = substream->runtime;
        pr_info("Opening PCM\n");

        err = dummy_hrtimer_ops.create(substream);
        if (err < 0) {
                pr_err("Failed to create timer\n");
                return -1;
        }

        get_timer_ops(substream) = &dummy_hrtimer_ops;

        runtime->hw = snd_soundgen_hw;
        chip->pcm_hw = runtime->hw;

        if (substream->pcm->device & 1) {
                runtime->hw.info &= ~SNDRV_PCM_INFO_INTERLEAVED;
                runtime->hw.info |= SNDRV_PCM_INFO_NONINTERLEAVED;
        }
        if (substream->pcm->device & 2)
                runtime->hw.info &= ~(SNDRV_PCM_INFO_MMAP |
                                SNDRV_PCM_INFO_MMAP_VALID);

        return 0;
}

static int soundgen_pcm_close(struct snd_pcm_substream *substream)
{
        pr_info("Closing PCM\n");
        get_timer_ops(substream)->free(substream);
        return 0;
}

static int soundgen_pcm_hw_params(struct snd_pcm_substream *substream,
                struct snd_pcm_hw_params *params)
{
        pr_info("HW Params\n");
        pr_debug("Buffer size %d\n", params_buffer_bytes(params));
        return snd_pcm_lib_malloc_pages(substream,
                        params_buffer_bytes(params));
}

static int soundgen_pcm_hw_free(struct snd_pcm_substream *substream)
{
        pr_info("HW free\n");
        return snd_pcm_lib_free_pages(substream);
}

static int soundgen_pcm_prepare(struct snd_pcm_substream *substream)
{
        struct snd_pcm_runtime *runtime = substream->runtime;
        pr_info("PCM Prepare\n");

        pr_debug("Format %d\n", runtime->format);
        pr_debug("Rate %d\n", runtime->rate);
        pr_debug("Channels %d\n", runtime->channels);
        pr_debug("Buffer size %ld\n", runtime->buffer_size);
        pr_debug("Period size %ld\n", runtime->period_size);

        return get_timer_ops(substream)->prepare(substream);
}

static int soundgen_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
        pr_info("PCM Trigger\n");
        pr_debug("Command: %d\n",cmd);
        switch (cmd) {
                case SNDRV_PCM_TRIGGER_START:
                        /* do something to start the PCM engine */
                        return get_timer_ops(substream)->start(substream);
                case SNDRV_PCM_TRIGGER_STOP:
                        return get_timer_ops(substream)->stop(substream); 
        }
        return -EINVAL;
}

static snd_pcm_uframes_t soundgen_pcm_pointer(struct snd_pcm_substream *substream)
{
        //pr_info("PCM Pointer\n");
        return get_timer_ops(substream)->pointer(substream);
}

#ifdef DEBUG
int soundgen_pcm_ioctl_wrap(struct snd_pcm_substream *substream,
                unsigned int cmd, void *arg)
{
        int err;
        pr_info("PCM IOCTL Wrapper");
        pr_debug("IOCTL Command: %d", cmd);
        err = snd_pcm_lib_ioctl(substream, cmd, arg);
        pr_debug("Return: %d",err);
        return err;
}
#endif

static const struct snd_pcm_ops snd_soundgen_capture_ops = {
        .open = soundgen_pcm_open,
        .close = soundgen_pcm_close,
#ifdef DEBUG
        .ioctl = soundgen_pcm_ioctl_wrap,
#else
        .ioctl = snd_pcm_lib_ioctl,
#endif
        .hw_params = soundgen_pcm_hw_params,
        .hw_free = soundgen_pcm_hw_free,
        .prepare = soundgen_pcm_prepare,
        .trigger = soundgen_pcm_trigger,
        .pointer = soundgen_pcm_pointer
};

static int snd_soundgen_new_pcm(struct snd_card_soundgen *soundgen_card)
{
        struct snd_pcm *pcm;
        int err;

        err = snd_pcm_new(soundgen_card->card, "Soundgen PCM", 0, 0,
                        1, &pcm);
        if (err < 0) {
                return err;
        }
        snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
                        &snd_soundgen_capture_ops);
        pcm->private_data = soundgen_card;
        strcpy(pcm->name, "Soundgen PCM");
        soundgen_card->pcm = pcm;
        snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
                        snd_dma_continuous_data(GFP_KERNEL),
                        0, (64*1024));
        return 0;
}

/**
 * End of PCM Stuff
 */

/**
 * Generic ALSA stuff
 */
static int snd_soundgen_driver_probe(struct platform_device *devptr)
{
        int err;
        struct snd_card *card;
        struct snd_card_soundgen *soundgen;

        dev_info(&devptr->dev, "Sound gen driver probed\n");
        err = snd_card_new(&devptr->dev, 0, NULL, 
                        THIS_MODULE, sizeof(struct snd_card_soundgen),
                        &card);
        if (err < 0) {
                dev_err(&devptr->dev, "Failed to create new soundcard\n");
                return err;
        }

        /* Link new snd_card and our snd_card_soundgen together */
        soundgen = card->private_data;
        soundgen->card = card;

        strcpy(card->driver, "Sound Gen");
        strcpy(card->shortname, "Sound Gen");
        sprintf(card->longname, "Virtual Sound generator");

        err = snd_soundgen_new_pcm(soundgen);
        if (err < 0) {
                dev_err(&devptr->dev, "Failed to create pcm\n");
                goto error;
        }

        err = snd_card_register(card);
        if (err < 0) {
                dev_err(&devptr->dev, "Failed to register soundcard\n");
                goto error;
        }

        platform_set_drvdata(devptr, card);
        return 0;

error:
        snd_card_free(card);
        return err;
}

static int snd_soundgen_driver_remove(struct platform_device *devptr)
{
        dev_info(&devptr->dev,"Sound gen driver removed\n");
        snd_card_free(platform_get_drvdata(devptr));
        return 0;
}

/**
 * End of generic ALSA Stuff
 */

/**
 * Module stuff
 */

#define SND_SOUNDGEN_DRIVER "snd_soundgen"

static struct platform_driver snd_soundgen_driver = {
        .probe = snd_soundgen_driver_probe,
        .remove = snd_soundgen_driver_remove,
        .driver = {
                .name = SND_SOUNDGEN_DRIVER, 
        },
};

static int __init alsa_soundgen_card_init(void)
{
        int err;

        err = platform_driver_register(&snd_soundgen_driver);
        if (err < 0) {
                pr_err("Faild to register platform driver\n");
                return err;
        }
        pr_info("Sound Generator platform device registered\n");

        device = platform_device_register_simple(SND_SOUNDGEN_DRIVER,
                        0, NULL, 0);
        if (IS_ERR(device)) {
                pr_err("Failed to register platform device\n");
                platform_driver_unregister(&snd_soundgen_driver);
                return PTR_ERR(device);
        }

        return 0;
}

static void __exit alsa_soundgen_card_exit(void)
{
        platform_device_unregister(device);
        platform_driver_unregister(&snd_soundgen_driver);        
}

module_init(alsa_soundgen_card_init)
module_exit(alsa_soundgen_card_exit)
