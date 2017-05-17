/*
 * linux/arch/arm/mach-omap2/board-omap3beagle.c
 *
 * Copyright (C) 2008 Texas Instruments
 *
 * Modified from mach-omap2/board-3430sdp.c
 *
 * Initial code: Syed Mohammed Khasim
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/leds.h>
#include <linux/pwm.h>
#include <linux/leds_pwm.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/input.h>
#include <linux/gpio_keys.h>
#include <linux/opp.h>
#include <linux/cpu.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/nand.h>
#include <linux/mmc/host.h>
#include <linux/usb/phy.h>

#include <linux/regulator/machine.h>
#include <linux/regulator/fixed.h>
#include <linux/i2c/twl.h>
#include <linux/i2c/at24.h>
#include <linux/i2c/pcf857x.h> //ADDED BVL

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/flash.h>
#include <linux/spi/spi.h>

#include <video/omapdss.h>
#include <video/omap-panel-generic-dpi.h>
#include <video/omap-panel-tfp410.h>
#include <linux/platform_data/mtd-nand-omap2.h>

#include "common.h"
#include "omap_device.h"
#include "gpmc.h"
#include "soc.h"
#include "mux.h"
#include "hsmmc.h"
#include "pm.h"
#include "board-flash.h"
#include "common-board-devices.h"

#define	NAND_CS	0

static struct pwm_lookup pwm_lookup[] = {
	/* LEDB -> PMU_STAT */
	PWM_LOOKUP("twl-pwmled", 1, "leds_pwm", "beagleboard::pmu_stat"),
};

static struct led_pwm pwm_leds[] = {
	{
		.name		= "beagleboard::pmu_stat",
		.max_brightness	= 127,
		.pwm_period_ns	= 7812500,
	},
};

static struct led_pwm_platform_data pwm_data = {
	.num_leds	= ARRAY_SIZE(pwm_leds),
	.leds		= pwm_leds,
};

static struct platform_device leds_pwm = {
	.name	= "leds_pwm",
	.id	= -1,
	.dev	= {
		.platform_data = &pwm_data,
	},
};

/*
 * OMAP3 Beagle revision
 * Run time detection of Beagle revision is done by reading GPIO.
 * GPIO ID -
 *	AXBX	= GPIO173, GPIO172, GPIO171: 1 1 1
 *	C1_3	= GPIO173, GPIO172, GPIO171: 1 1 0
 *	C4	= GPIO173, GPIO172, GPIO171: 1 0 1
 *	XMA/XMB = GPIO173, GPIO172, GPIO171: 0 0 0
 *	XMC = GPIO173, GPIO172, GPIO171: 0 1 0
 */
enum {
	OMAP3BEAGLE_BOARD_UNKN = 0,
	OMAP3BEAGLE_BOARD_AXBX,
	OMAP3BEAGLE_BOARD_C1_3,
	OMAP3BEAGLE_BOARD_C4,
	OMAP3BEAGLE_BOARD_XM,
	OMAP3BEAGLE_BOARD_XMC,
};

static u8 omap3_beagle_version;

/*
 * Board-specific configuration
 * Defaults to BeagleBoard-xMC
 */
static struct {
	int mmc1_gpio_wp;
	int usb_pwr_level;
	int dvi_pd_gpio;
	int usr_button_gpio;
	int mmc_caps;
	char *lcd_driver_name;
	int lcd_pwren;
} beagle_config = {
	.mmc1_gpio_wp = -EINVAL,
	.usb_pwr_level = GPIOF_OUT_INIT_LOW,
	.dvi_pd_gpio = -EINVAL,
	.usr_button_gpio = 4,
	.mmc_caps = MMC_CAP_4_BIT_DATA | MMC_CAP_8_BIT_DATA,
	.lcd_driver_name = "",
	.lcd_pwren = 156,
};

static struct gpio omap3_beagle_rev_gpios[] __initdata = {
	{ 171, GPIOF_IN, "rev_id_0"    },
	{ 172, GPIOF_IN, "rev_id_1" },
	{ 173, GPIOF_IN, "rev_id_2"    },
};

static void __init omap3_beagle_init_rev(void)
{
	int ret;
	u16 beagle_rev = 0;

	omap_mux_init_gpio(171, OMAP_PIN_INPUT_PULLUP);
	omap_mux_init_gpio(172, OMAP_PIN_INPUT_PULLUP);
	omap_mux_init_gpio(173, OMAP_PIN_INPUT_PULLUP);

	ret = gpio_request_array(omap3_beagle_rev_gpios,
				 ARRAY_SIZE(omap3_beagle_rev_gpios));
	if (ret < 0) {
		printk(KERN_ERR "Unable to get revision detection GPIO pins\n");
		omap3_beagle_version = OMAP3BEAGLE_BOARD_UNKN;
		return;
	}

	beagle_rev = gpio_get_value(171) | (gpio_get_value(172) << 1)
			| (gpio_get_value(173) << 2);

	gpio_free_array(omap3_beagle_rev_gpios,
			ARRAY_SIZE(omap3_beagle_rev_gpios));

	switch (beagle_rev) {
	case 7:
		printk(KERN_INFO "OMAP3 Beagle Rev: Ax/Bx\n");
		omap3_beagle_version = OMAP3BEAGLE_BOARD_AXBX;
		beagle_config.mmc1_gpio_wp = 29;
		beagle_config.dvi_pd_gpio = 170;
		beagle_config.usr_button_gpio = 7;
		break;
	case 6:
		printk(KERN_INFO "OMAP3 Beagle Rev: C1/C2/C3\n");
		omap3_beagle_version = OMAP3BEAGLE_BOARD_C1_3;
		beagle_config.mmc1_gpio_wp = 23;
		beagle_config.dvi_pd_gpio = 170;
		beagle_config.usr_button_gpio = 7;
		break;
	case 5:
		printk(KERN_INFO "OMAP3 Beagle Rev: C4\n");
		omap3_beagle_version = OMAP3BEAGLE_BOARD_C4;
		beagle_config.mmc1_gpio_wp = 23;
		beagle_config.dvi_pd_gpio = 170;
		beagle_config.usr_button_gpio = 7;
		break;
	case 0:
		printk(KERN_INFO "OMAP3 Beagle Rev: xM Ax/Bx\n");
		omap3_beagle_version = OMAP3BEAGLE_BOARD_XM;
		beagle_config.usb_pwr_level = GPIOF_OUT_INIT_HIGH;
		beagle_config.mmc_caps &= ~MMC_CAP_8_BIT_DATA;
		break;
	case 2:
		printk(KERN_INFO "OMAP3 Beagle Rev: xM C\n");
		omap3_beagle_version = OMAP3BEAGLE_BOARD_XMC;
		beagle_config.mmc_caps &= ~MMC_CAP_8_BIT_DATA;
		break;
	default:
		printk(KERN_INFO "OMAP3 Beagle Rev: unknown %hd\n", beagle_rev);
		omap3_beagle_version = OMAP3BEAGLE_BOARD_UNKN;
	}
}

char expansionboard_name[16];
char expansionboard2_name[16];
char camera_name[16];
char wl12xx_name[16];

enum {
	EXPANSION_MMC_NONE = 0,
	EXPANSION_MMC_ZIPPY,
	EXPANSION_MMC_WIFI,
};

enum {
	EXPANSION_I2C_NONE = 0,
	EXPANSION_I2C_ZIPPY,
	EXPANSION_I2C_7ULCD,
	EXPANSION_I2C_COM6L,
};

//////////////////////////////////BVL//////////////////////////////////////
#define GPIO_EXPANDER_BASE 200

static struct pcf857x_platform_data pca8575_data[] = {
	{.gpio_base = GPIO_EXPANDER_BASE, .n_latch = 0,},
	{.gpio_base = GPIO_EXPANDER_BASE + 16, .n_latch = 0,},
	{.gpio_base = GPIO_EXPANDER_BASE + 32, .n_latch = 0,},
};

static struct i2c_board_info __initdata i2c_expanders_devices[] = {
	{
		I2C_BOARD_INFO("pca8575",0x20),
		.platform_data = &pca8575_data[0],
	},
	{
		I2C_BOARD_INFO("pca8575",0x21),
		.platform_data = &pca8575_data[1],
	},
	{
		I2C_BOARD_INFO("pca8575",0x22),
		.platform_data = &pca8575_data[2],
	},
};
//////////////////////////////////////////////////////////////////////////



static struct {
	int mmc_settings;
	int i2c_settings;
} expansion_config = {
	.mmc_settings = EXPANSION_MMC_NONE,
	.i2c_settings = EXPANSION_I2C_NONE,
};

#if defined(CONFIG_WL12XX) || defined(CONFIG_WL12XX_MODULE)
#include <linux/regulator/fixed.h>
#include <linux/ti_wilink_st.h>
#include <linux/wl12xx.h>

#define OMAP_BEAGLE_WLAN_EN_GPIO    (139)
#define OMAP_BEAGLE_BT_EN_GPIO      (138)
#define OMAP_BEAGLE_WLAN_IRQ_GPIO   (137)
#define OMAP_BEAGLE_FM_EN_BT_WU     (136)

struct wl12xx_platform_data omap_beagle_wlan_data __initdata = {
	.board_ref_clock = WL12XX_REFCLOCK_38, /* 38.4 MHz */
};

struct wl12xx_platform_data omap_beagle_wlan_data_26mhz __initdata = {
	.board_ref_clock = WL12XX_REFCLOCK_26, /* 26 MHz */
};

static struct ti_st_plat_data wilink_platform_data = {
	.nshutdown_gpio	= OMAP_BEAGLE_BT_EN_GPIO,
	.dev_name		= "/dev/ttyO1",
	.flow_cntrl		= 1,
	.baud_rate		= 3000000,
	.chip_enable	= NULL,
	.suspend		= NULL,
	.resume			= NULL,
};

static struct platform_device wl12xx_device = {
		.name		= "kim",
		.id			= -1,
		.dev.platform_data = &wilink_platform_data,
};

static struct platform_device btwilink_device = {
	.name	= "btwilink",
	.id	= -1,
};
#endif

static struct regulator_consumer_supply beagle_vmmc2_supply =
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.1");

static struct regulator_init_data beagle_vmmc2 = {
	.constraints = {
		.valid_ops_mask = REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies = 1,
	.consumer_supplies = &beagle_vmmc2_supply,
};

#if defined(CONFIG_WL12XX) || defined(CONFIG_WL12XX_MODULE)
static struct fixed_voltage_config beagle_vwlan = {
	.supply_name = "vwl1271",
	.microvolts = 1800000,  /* 1.8V */
	.gpio = OMAP_BEAGLE_WLAN_EN_GPIO,
	.startup_delay = 70000, /* 70ms */
	.enable_high = 1,
	.enabled_at_boot = 0,
	.init_data = &beagle_vmmc2,
};

static struct platform_device omap_vwlan_device = {
	.name		= "reg-fixed-voltage",
	.id		= 1,
	.dev = {
		.platform_data = &beagle_vwlan,
	},
};
#endif

//rcn-ee: this is just a fake regulator, the zippy hardware provides 3.3/1.8 with jumper..
static struct fixed_voltage_config beagle_vzippy = {
	.supply_name = "vzippy",
	.microvolts = 3300000,  /* 3.3V */
	.startup_delay = 70000, /* 70ms */
	.enable_high = 1,
	.enabled_at_boot = 0,
	.init_data = &beagle_vmmc2,
};

static struct platform_device omap_zippy_device = {
	.name	= "reg-fixed-voltage",
	.id		= 1,
	.dev = {
		.platform_data = &beagle_vzippy,
	},
};

#define OMAP3BEAGLE_GPIO_ZIPPY_MMC_WP 141
#define OMAP3BEAGLE_GPIO_ZIPPY_MMC_CD 162

#if defined(CONFIG_ENC28J60) || defined(CONFIG_ENC28J60_MODULE)
#include <linux/platform_data/spi-omap2-mcspi.h>
#include <linux/spi/spi.h>

#define OMAP3BEAGLE_GPIO_ENC28J60_IRQ 157

static struct omap2_mcspi_device_config enc28j60_spi_chip_info = {
	.turbo_mode	= 0,
};

static struct spi_board_info omap3beagle_zippy_spi_board_info[] __initdata = {
	{
		.modalias		= "enc28j60",
		.bus_num		= 4,
		.chip_select	= 0,
		.max_speed_hz	= 20000000,
		.controller_data	= &enc28j60_spi_chip_info,
	},
};

static void __init omap3beagle_enc28j60_init(void)
{
	if ((gpio_request(OMAP3BEAGLE_GPIO_ENC28J60_IRQ, "ENC28J60_IRQ") == 0) &&
	    (gpio_direction_input(OMAP3BEAGLE_GPIO_ENC28J60_IRQ) == 0)) {
		gpio_export(OMAP3BEAGLE_GPIO_ENC28J60_IRQ, 0);
		omap3beagle_zippy_spi_board_info[0].irq = gpio_to_irq(OMAP3BEAGLE_GPIO_ENC28J60_IRQ);
		irq_set_irq_type(omap3beagle_zippy_spi_board_info[0].irq, IRQ_TYPE_EDGE_FALLING);
	} else {
		pr_err("Beagle expansionboard: could not obtain gpio for ENC28J60_IRQ\n");
		return;
	}

	spi_register_board_info(omap3beagle_zippy_spi_board_info,
			ARRAY_SIZE(omap3beagle_zippy_spi_board_info));
}

#else
static inline void __init omap3beagle_enc28j60_init(void) { return; }
#endif

#if defined(CONFIG_KS8851) || defined(CONFIG_KS8851_MODULE)
#include <linux/platform_data/spi-omap2-mcspi.h>
#include <linux/spi/spi.h>

#define OMAP3BEAGLE_GPIO_KS8851_IRQ 157

static struct omap2_mcspi_device_config ks8851_spi_chip_info = {
	.turbo_mode	= 0,
};

static struct spi_board_info omap3beagle_zippy2_spi_board_info[] __initdata = {
	{
		.modalias		= "ks8851",
		.bus_num		= 4,
		.chip_select	= 0,
		.max_speed_hz	= 36000000,
		.controller_data	= &ks8851_spi_chip_info,
	},
};

static void __init omap3beagle_ks8851_init(void)
{
	if ((gpio_request(OMAP3BEAGLE_GPIO_KS8851_IRQ, "KS8851_IRQ") == 0) &&
	    (gpio_direction_input(OMAP3BEAGLE_GPIO_KS8851_IRQ) == 0)) {
		gpio_export(OMAP3BEAGLE_GPIO_KS8851_IRQ, 0);
		omap3beagle_zippy2_spi_board_info[0].irq = gpio_to_irq(OMAP3BEAGLE_GPIO_KS8851_IRQ);
		irq_set_irq_type(omap3beagle_zippy2_spi_board_info[0].irq, IRQ_TYPE_EDGE_FALLING);
	} else {
		pr_err("Beagle expansionboard: could not obtain gpio for KS8851_IRQ\n");
		return;
	}

	spi_register_board_info(omap3beagle_zippy2_spi_board_info,
			ARRAY_SIZE(omap3beagle_zippy2_spi_board_info));
}

#else
static inline void __init omap3beagle_ks8851_init(void) { return; }
#endif

static struct mtd_partition omap3beagle_nand_partitions[] = {
	/* All the partition sizes are listed in terms of NAND block size */
	{
		.name		= "X-Loader",
		.offset		= 0,
		.size		= 4 * NAND_BLOCK_SIZE,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "U-Boot",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x80000 */
		.size		= 15 * NAND_BLOCK_SIZE,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	},
	{
		.name		= "U-Boot Env",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x260000 */
		.size		= 1 * NAND_BLOCK_SIZE,
	},
	{
		.name		= "Kernel",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x280000 */
		.size		= 32 * NAND_BLOCK_SIZE,
	},
	{
		.name		= "File System",
		.offset		= MTDPART_OFS_APPEND,	/* Offset = 0x680000 */
		.size		= MTDPART_SIZ_FULL,
	},
};

/* DSS */

static struct tfp410_platform_data dvi_panel = {
	.i2c_bus_num = 3,
	.power_down_gpio = -1,
};

static struct omap_dss_device beagle_dvi_device = {
	.type = OMAP_DISPLAY_TYPE_DPI,
	.name = "dvi",
	.driver_name = "tfp410",
	.data = &dvi_panel,
	.phy.dpi.data_lines = 24,
};

static struct omap_dss_device beagle_tv_device = {
	.name = "tv",
	.driver_name = "venc",
	.type = OMAP_DISPLAY_TYPE_VENC,
	.phy.venc.type = OMAP_DSS_VENC_TYPE_SVIDEO,
};

static int beagle_enable_lcd(struct omap_dss_device *dssdev)
{
	if (gpio_is_valid(beagle_config.lcd_pwren)) {
		pr_info("%s: Enabling LCD\n", __FUNCTION__);
		gpio_set_value(beagle_config.lcd_pwren, 0);
	} else {
		pr_info("%s: Invalid LCD enable GPIO: %d\n",
			__FUNCTION__, beagle_config.lcd_pwren);
	}

	return 0;
}

static void beagle_disable_lcd(struct omap_dss_device *dssdev)
{
	if (gpio_is_valid(beagle_config.lcd_pwren)) {
		pr_info("%s: Disabling LCD\n", __FUNCTION__);
		gpio_set_value(beagle_config.lcd_pwren, 1);
	} else {
		pr_info("%s: Invalid LCD enable GPIO: %d\n",
			__FUNCTION__, beagle_config.lcd_pwren);
	}

	return;
}

static struct panel_generic_dpi_data lcd_panel = {
	.name = "tfc_s9700rtwv35tr-01b",
	.platform_enable = beagle_enable_lcd,
	.platform_disable = beagle_disable_lcd,
};

static struct omap_dss_device beagle_lcd_device = {
	.type                   = OMAP_DISPLAY_TYPE_DPI,
	.name                   = "lcd",
	.driver_name		= "generic_dpi_panel",
	.phy.dpi.data_lines     = 24,
	.platform_enable        = beagle_enable_lcd,
	.platform_disable       = beagle_disable_lcd,
	.reset_gpio 		= -EINVAL,
	.data			= &lcd_panel,
};

static struct omap_dss_device *beagle_dss_devices[] = {
	&beagle_dvi_device,
	&beagle_tv_device,
	&beagle_lcd_device,
};

static struct omap_dss_board_info beagle_dss_data = {
	.num_devices = ARRAY_SIZE(beagle_dss_devices),
	.devices = beagle_dss_devices,
	.default_device = &beagle_dvi_device,
};

#include "sdram-micron-mt46h32m32lf-6.h"

static struct omap2_hsmmc_info mmc[] = {
	{
		.mmc		= 1,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_wp	= -EINVAL,
		.deferred	= true,
	},
	{}	/* Terminator */
};

static struct omap2_hsmmc_info mmc_zippy[] = {
	{
		.mmc		= 1,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_wp	= -EINVAL,
		.deferred	= true,
	},
	{
		.mmc		= 2,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_wp	= OMAP3BEAGLE_GPIO_ZIPPY_MMC_WP,
		.gpio_cd	= OMAP3BEAGLE_GPIO_ZIPPY_MMC_CD,
		.transceiver	= true,
		.deferred	= true,
	},
	{}	/* Terminator */
};

static struct omap2_hsmmc_info mmcbbt[] = {
	{
		.mmc		= 1,
		.caps		= MMC_CAP_4_BIT_DATA,
		.gpio_wp	= -EINVAL,
		.deferred	= true,
	},
	{
		.name		= "wl1271",
		.mmc		= 2,
		.caps		= MMC_CAP_4_BIT_DATA | MMC_CAP_POWER_OFF_CARD,
		.gpio_wp	= -EINVAL,
		.gpio_cd	= -EINVAL,
		.ocr_mask	= MMC_VDD_165_195,
		.nonremovable	= true,
	},
	{}	/* Terminator */
};

static struct regulator_consumer_supply beagle_vmmc1_supply[] = {
	REGULATOR_SUPPLY("vmmc", "omap_hsmmc.0"),
};

static struct regulator_consumer_supply beagle_vsim_supply[] = {
	REGULATOR_SUPPLY("vmmc_aux", "omap_hsmmc.0"),
};

static struct regulator_consumer_supply beagle_vaux3_supply = {
	.supply		= "cam_1v8",
};

static struct regulator_consumer_supply beagle_vaux4_supply = {
	.supply		= "cam_2v8",
};

static struct gpio_led gpio_leds[];

static int beagle_twl_gpio_setup(struct device *dev,
		unsigned gpio, unsigned ngpio)
{
	int r;

	switch (expansion_config.mmc_settings) {
	case EXPANSION_MMC_WIFI:
		mmcbbt[0].gpio_wp = beagle_config.mmc1_gpio_wp;
		/* gpio + 0 is "mmc0_cd" (input/IRQ) */
		mmcbbt[0].gpio_cd = gpio + 0;

		omap_hsmmc_late_init(mmcbbt);
		break;
	case EXPANSION_MMC_ZIPPY:
		mmc_zippy[0].gpio_wp = beagle_config.mmc1_gpio_wp;
		/* gpio + 0 is "mmc0_cd" (input/IRQ) */
		mmc_zippy[0].gpio_cd = gpio + 0;

		omap_hsmmc_late_init(mmc_zippy);
		break;
	default:
		mmc[0].gpio_wp = beagle_config.mmc1_gpio_wp;
		/* gpio + 0 is "mmc0_cd" (input/IRQ) */
		mmc[0].gpio_cd = gpio + 0;

		omap_hsmmc_late_init(mmc);
	}

	/*
	 * TWL4030_GPIO_MAX + 0 == ledA, EHCI nEN_USB_PWR (out, XM active
	 * high / others active low)
	 * DVI reset GPIO is different between beagle revisions
	 */
	/* Valid for all -xM revisions */
	if (cpu_is_omap3630()) {
		/*
		 * gpio + 1 on Xm controls the TFP410's enable line (active low)
		 * gpio + 2 control varies depending on the board rev as below:
		 * P7/P8 revisions(prototype): Camera EN
		 * A2+ revisions (production): LDO (DVI, serial, led blocks)
		 */
		r = gpio_request_one(gpio + 1, GPIOF_OUT_INIT_LOW,
				     "nDVI_PWR_EN");
		if (r)
			pr_err("%s: unable to configure nDVI_PWR_EN\n",
				__func__);

		beagle_config.dvi_pd_gpio = gpio + 2;

	} else {
		/*
		 * REVISIT: need ehci-omap hooks for external VBUS
		 * power switch and overcurrent detect
		 */
		if (gpio_request_one(gpio + 1, GPIOF_IN, "EHCI_nOC"))
			pr_err("%s: unable to configure EHCI_nOC\n", __func__);
	}
	dvi_panel.power_down_gpio = beagle_config.dvi_pd_gpio;

	gpio_request_one(gpio + TWL4030_GPIO_MAX, beagle_config.usb_pwr_level,
			"nEN_USB_PWR");

	return 0;
}

static struct twl4030_gpio_platform_data beagle_gpio_data = {
	.use_leds	= true,
	.pullups	= BIT(1),
	.pulldowns	= BIT(2) | BIT(6) | BIT(7) | BIT(8) | BIT(13)
				| BIT(15) | BIT(16) | BIT(17),
	.setup		= beagle_twl_gpio_setup,
};

/* VMMC1 for MMC1 pins CMD, CLK, DAT0..DAT3 (20 mA, plus card == max 220 mA) */
static struct regulator_init_data beagle_vmmc1 = {
	.constraints = {
		.min_uV			= 1850000,
		.max_uV			= 3150000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(beagle_vmmc1_supply),
	.consumer_supplies	= beagle_vmmc1_supply,
};

/* VSIM for MMC1 pins DAT4..DAT7 (2 mA, plus card == max 50 mA) */
static struct regulator_init_data beagle_vsim = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 3000000,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_VOLTAGE
					| REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= ARRAY_SIZE(beagle_vsim_supply),
	.consumer_supplies	= beagle_vsim_supply,
};

/* VAUX3 for CAM_1V8 */
static struct regulator_init_data beagle_vaux3 = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &beagle_vaux3_supply,
};

/* VAUX4 for CAM_2V8 */
static struct regulator_init_data beagle_vaux4 = {
	.constraints = {
		.min_uV			= 1800000,
		.max_uV			= 1800000,
		.apply_uV		= true,
		.valid_modes_mask	= REGULATOR_MODE_NORMAL
					| REGULATOR_MODE_STANDBY,
		.valid_ops_mask		= REGULATOR_CHANGE_MODE
					| REGULATOR_CHANGE_STATUS,
	},
	.num_consumer_supplies	= 1,
	.consumer_supplies	= &beagle_vaux4_supply,
};

static struct twl4030_platform_data beagle_twldata = {
	/* platform_data for children goes here */
	.gpio		= &beagle_gpio_data,
	.vmmc1		= &beagle_vmmc1,
	.vsim		= &beagle_vsim,
	.vaux3		= &beagle_vaux3,
	.vaux4		= &beagle_vaux4,
};

static struct i2c_board_info __initdata beagle_i2c_eeprom[] = {
       {
               I2C_BOARD_INFO("eeprom", 0x50),
       },
};

static struct i2c_board_info __initdata zippy_i2c2_rtc[] = {
#if defined(CONFIG_RTC_DRV_DS1307) || defined(CONFIG_RTC_DRV_DS1307_MODULE)
	{
		I2C_BOARD_INFO("ds1307", 0x68),
	},
#endif
};

#if defined(CONFIG_TOUCHSCREEN_TSC2007) || defined(CONFIG_TOUCHSCREEN_TSC2007_MODULE)
/* Touchscreen */
#include <linux/i2c/tsc2007.h>

#define OMAP3BEAGLE_TSC2007_GPIO 157

static int omap3beagle_tsc2007_get_pendown_state(void)
{
	return !gpio_get_value(OMAP3BEAGLE_TSC2007_GPIO);
}

static struct tsc2007_platform_data tsc2007_info = {
	.model = 2007,
	.x_plate_ohms = 180,
	.get_pendown_state = omap3beagle_tsc2007_get_pendown_state,
};

static struct i2c_board_info __initdata beagle_i2c2_bbtoys_ulcd[] = {
	{
		I2C_BOARD_INFO("tlc59108", 0x40),
	},
	{
		I2C_BOARD_INFO("tsc2007", 0x48),
		.platform_data = &tsc2007_info,
	},
};

static void __init omap3beagle_tsc2007_init(void)
{
	int r;

	omap_mux_init_gpio(OMAP3BEAGLE_TSC2007_GPIO, OMAP_PIN_INPUT_PULLUP);

	r = gpio_request_one(OMAP3BEAGLE_TSC2007_GPIO, GPIOF_IN, "tsc2007_pen_down");
	if (r < 0) {
		pr_err("Beagle expansionboard: failed to request GPIO#%d for "
		"tsc2007 pen down IRQ\n", OMAP3BEAGLE_TSC2007_GPIO);
		return;
	}

	beagle_i2c2_bbtoys_ulcd[0].irq = gpio_to_irq(OMAP3BEAGLE_TSC2007_GPIO);
	irq_set_irq_type(gpio_to_irq(OMAP3BEAGLE_TSC2007_GPIO), IRQ_TYPE_EDGE_FALLING);
}
#else
static struct i2c_board_info __initdata beagle_i2c2_bbtoys_ulcd[] = {};
#endif

#if defined(CONFIG_VIDEO_MT9P031)
/* needed for: omap3_beagle_late_initcall */
#include "devices.h"
#include <media/omap3isp.h>
#include <media/mt9p031.h>
/* needed for: v4l2_dev_to_isp_device */
#include "../../../drivers/media/platform/omap3isp/isp.h"

#define MT9P031_XCLK		ISP_XCLK_A

#define MT9P031_RESET_GPIO	98
#define MT9P031_EXT_FREQ	21000000
#define MT9P031_TARGET_FREQ	48000000

#define MT9P031_I2C_ADDR	0x48
#define MT9P031_I2C_BUS		2

static struct regulator *reg_1v8, *reg_2v8;

static int beagle_cam_set_xclk(struct v4l2_subdev *subdev, int hz)
{
	struct isp_device *isp = v4l2_dev_to_isp_device(subdev->v4l2_dev);

	return isp->platform_cb.set_xclk(isp, hz, MT9P031_XCLK);
}

static struct mt9p031_platform_data beagle_mt9p031_platform_data = {
	.set_xclk	= beagle_cam_set_xclk,
	.reset		= MT9P031_RESET_GPIO,
	.ext_freq	= MT9P031_EXT_FREQ,
	.target_freq	= MT9P031_TARGET_FREQ,
};

static struct i2c_board_info mt9p031_camera_i2c_device = {
	I2C_BOARD_INFO("mt9p031", MT9P031_I2C_ADDR),
	.platform_data = &beagle_mt9p031_platform_data,
};

static struct isp_subdev_i2c_board_info mt9p031_camera_subdevs[] = {
	{
		.board_info = &mt9p031_camera_i2c_device,
		.i2c_adapter_id = MT9P031_I2C_BUS,
	},
	{ NULL, 0, },
};

static struct isp_v4l2_subdevs_group beagle_camera_subdevs[] = {
	{
		.subdevs = mt9p031_camera_subdevs,
		.interface = ISP_INTERFACE_PARALLEL,
		.bus = {
			.parallel = {
				.data_lane_shift = 0,
				.clk_pol = 1,
			}
		},
	},
	{ },
};

static struct isp_platform_data beagle_isp_platform_data = {
	.subdevs = beagle_camera_subdevs,
};
#endif

static struct at24_platform_data beagle_at24_eeprom_info = {
	.byte_len	= (256*1024) / 8,
	.page_size	= 64,
	.flags		= AT24_FLAG_ADDR16,
	.context	= (void *)NULL,
};

static struct i2c_board_info __initdata com6l_adpt_eeprom[] = {
	{
		I2C_BOARD_INFO("24c256", 0x50),
		.platform_data  = &beagle_at24_eeprom_info,
	},
};

static int __init omap3_beagle_i2c_init(void)
{
	omap3_pmic_get_config(&beagle_twldata,
			TWL_COMMON_PDATA_USB | TWL_COMMON_PDATA_MADC |
			TWL_COMMON_PDATA_AUDIO | TWL_COMMON_PDATA_POWER,
			TWL_COMMON_REGULATOR_VDAC | TWL_COMMON_REGULATOR_VPLL2);

	beagle_twldata.vpll2->constraints.name = "VDVI";

	omap3_pmic_init("twl4030", &beagle_twldata);

	switch (expansion_config.i2c_settings) {
	case EXPANSION_I2C_7ULCD:
		omap_register_i2c_bus(2, 400,  beagle_i2c2_bbtoys_ulcd,
							ARRAY_SIZE(beagle_i2c2_bbtoys_ulcd));
		break;
	case EXPANSION_I2C_ZIPPY:
		omap_register_i2c_bus(2, 400, zippy_i2c2_rtc, ARRAY_SIZE(zippy_i2c2_rtc));
		break;
	case EXPANSION_I2C_COM6L:
		omap_register_i2c_bus(2, 400, com6l_adpt_eeprom, ARRAY_SIZE(com6l_adpt_eeprom));
		break;
	default:
		omap_register_i2c_bus(2, 400, NULL, 0);
	}

	/* Bus 3 is attached to the DVI port where devices like the pico DLP
	 * projector don't work reliably with 400kHz */
	//omap_register_i2c_bus(3, 100, beagle_i2c_eeprom, ARRAY_SIZE(beagle_i2c_eeprom));
	omap_register_i2c_bus(3,100,i2c_expanders_devices, ARRAY_SIZE(i2c_expanders_devices));
	return 0;
}

static struct gpio_led gpio_leds[] = {
	{
		.name			= "beagleboard::usr0",
		.default_trigger	= "heartbeat",
		.gpio			= 150,
	},
	{
		.name			= "beagleboard::usr1",
		.default_trigger	= "mmc0",
		.gpio			= 149,
	},
};

static struct gpio_led_platform_data gpio_led_info = {
	.leds		= gpio_leds,
	.num_leds	= ARRAY_SIZE(gpio_leds),
};

static struct platform_device leds_gpio = {
	.name	= "leds-gpio",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_led_info,
	},
};

static struct gpio_keys_button gpio_buttons[] = {
	{
		.code			= BTN_EXTRA,
		/* Dynamically assigned depending on board */
		.gpio			= -EINVAL,
		.desc			= "user",
		.wakeup			= 1,
	},
};

static struct gpio_keys_platform_data gpio_key_info = {
	.buttons	= gpio_buttons,
	.nbuttons	= ARRAY_SIZE(gpio_buttons),
};

static struct platform_device keys_gpio = {
	.name	= "gpio-keys",
	.id	= -1,
	.dev	= {
		.platform_data	= &gpio_key_info,
	},
};

static struct platform_device madc_hwmon = {
	.name	= "twl4030_madc_hwmon",
	.id	= -1,
};

static struct platform_device *omap3_beagle_devices[] __initdata = {
	&leds_gpio,
	&keys_gpio,
	&madc_hwmon,
	&leds_pwm,
};

static struct usbhs_omap_platform_data usbhs_bdata __initdata = {

	.port_mode[0] = OMAP_USBHS_PORT_MODE_UNUSED,
	.port_mode[1] = OMAP_EHCI_PORT_MODE_PHY,
	.port_mode[2] = OMAP_USBHS_PORT_MODE_UNUSED,

	.phy_reset  = true,
	.reset_gpio_port[0]  = -EINVAL,
	.reset_gpio_port[1]  = 147,
	.reset_gpio_port[2]  = -EINVAL
};

#ifdef CONFIG_OMAP_MUX
static struct omap_board_mux board_mux[] __initdata = {

	/*SPI */
	OMAP3_MUX(SDMMC2_CLK, OMAP_MUX_MODE1 | OMAP_PIN_INPUT), //spi3_clk
	OMAP3_MUX(SDMMC2_CMD, OMAP_MUX_MODE1 | OMAP_PIN_OUTPUT), //spi3_simo
	OMAP3_MUX(SDMMC2_DAT0, OMAP_MUX_MODE1 | OMAP_PIN_INPUT_PULLUP), //spi3_somi
	OMAP3_MUX(SDMMC2_DAT3, OMAP_MUX_MODE1 | OMAP_PIN_OUTPUT), //spi3_cs0
	OMAP3_MUX(SDMMC2_DAT2, OMAP_MUX_MODE1 | OMAP_PIN_OUTPUT), //spi3_cs1
	
	/*gpio entries main expansion header*/

	OMAP3_MUX(SDMMC2_DAT7, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //139,
	OMAP3_MUX(UART2_CTS, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //144,
	OMAP3_MUX(SDMMC2_DAT6, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //138,
	OMAP3_MUX(UART2_TX, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //146,
	OMAP3_MUX(SDMMC2_DAT5, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //137,
	OMAP3_MUX(MCBSP3_FSX, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //143,
	OMAP3_MUX(SDMMC2_DAT4, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //136,
	OMAP3_MUX(UART2_RTS, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //145,
	OMAP3_MUX(MCBSP1_DX, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //158
	OMAP3_MUX(MCBSP1_CLKX, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //162,
	OMAP3_MUX(SDMMC2_DAT1, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //133
	OMAP3_MUX(MCBSP1_FSX, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //161
	OMAP3_MUX(MCBSP1_DR, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //159
	OMAP3_MUX(MCBSP1_FSR, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //157,
        
       /*gpio entries P17 auxiliary expansion header*/

	OMAP3_MUX(ETK_D6, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //20,
	OMAP3_MUX(ETK_D7, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //21,
	OMAP3_MUX(ETK_D3, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //17,
	OMAP3_MUX(ETK_D2, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //16,
	OMAP3_MUX(ETK_D1, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //15,
	OMAP3_MUX(ETK_D5, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //19,
	OMAP3_MUX(ETK_D9, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //23,
	OMAP3_MUX(ETK_D0, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //14,
	OMAP3_MUX(ETK_D4, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //18,
	OMAP3_MUX(ETK_CTL, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //13,
	OMAP3_MUX(ETK_D8, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //22,
	OMAP3_MUX(ETK_CLK, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //12,
	OMAP3_MUX(HDQ_SIO, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //170,
	OMAP3_MUX(GPMC_NCS6, OMAP_MUX_MODE4 | OMAP_PIN_INPUT_PULLUP), //57,	
        
	{ .reg_offset = OMAP_MUX_TERMINATOR },
};
#endif

static int __init expansionboard_setup(char *str)
{
	if (!machine_is_omap3_beagle())
		return 0;

	if (!str)
		return -EINVAL;
	strncpy(expansionboard_name, str, 16);
	pr_info("Beagle expansionboard: %s\n", expansionboard_name);
	return 0;
}

static int __init expansionboard2_setup(char *str)
{
	if (!machine_is_omap3_beagle())
		return 0;

	if (!str)
		return -EINVAL;
	strncpy(expansionboard2_name, str, 16);
	pr_info("Beagle expansionboard2: %s\n", expansionboard2_name);
	return 0;
}

static int __init camera_setup(char *str)
{
	if (!machine_is_omap3_beagle())
		return 0;

	if (!str)
		return -EINVAL;
	strncpy(camera_name, str, 16);
	pr_info("Beagle camera: %s\n", camera_name);
	return 0;
}

static int __init wl12xx_setup(char *str)
{
	if (!machine_is_omap3_beagle())
		return 0;

	if (!str)
		return -EINVAL;
	strncpy(wl12xx_name, str, 16);
	pr_info("Beagle wl12xx clk: %s\n", wl12xx_name);
	return 0;
}

static int __init beagle_opp_init(void)
{
	int r = 0;

	if (!machine_is_omap3_beagle())
		return 0;

	/* Initialize the omap3 opp table if not already created. */
	r = omap3_opp_init();
	if (IS_ERR_VALUE(r) && (r != -EEXIST)) {
		pr_err("%s: opp default init failed\n", __func__);
		return r;
	}

	/* Custom OPP enabled for all xM versions */
	if (cpu_is_omap3630()) {
		struct device *mpu_dev, *iva_dev;

		mpu_dev = get_cpu_device(0);
		iva_dev = omap_device_get_by_hwmod_name("iva");

		if (IS_ERR(mpu_dev) || IS_ERR(iva_dev)) {
			pr_err("%s: Aiee.. no mpu/dsp devices? %p %p\n",
				__func__, mpu_dev, iva_dev);
			return -ENODEV;
		}
		/* Enable MPU 1GHz and lower opps */
		r = opp_enable(mpu_dev, 800000000);
		/* TODO: MPU 1GHz needs SR and ABB */

		/* Enable IVA 800MHz and lower opps */
		r |= opp_enable(iva_dev, 660000000);
		/* TODO: DSP 800MHz needs SR and ABB */
		if (r) {
			pr_err("%s: failed to enable higher opp %d\n",
				__func__, r);
			/*
			 * Cleanup - disable the higher freqs - we dont care
			 * about the results
			 */
			opp_disable(mpu_dev, 800000000);
			opp_disable(iva_dev, 660000000);
		}
	}
	return 0;
}
omap_device_initcall(beagle_opp_init);

static void __init omap3_beagle_config_mcspi3_mux(void)
{
	/* NOTE: Clock pins need to be in input mode */
	omap_mux_init_signal("sdmmc2_clk.mcspi3_clk", OMAP_PIN_INPUT);
	omap_mux_init_signal("sdmmc2_cmd.mcspi3_simo", OMAP_PIN_OUTPUT);
	omap_mux_init_signal("sdmmc2_dat0.mcspi3_somi", OMAP_PIN_INPUT_PULLUP);
	omap_mux_init_signal("sdmmc2_dat2.mcspi3_cs1", OMAP_PIN_OUTPUT);
	omap_mux_init_signal("sdmmc2_dat3.mcspi3_cs0", OMAP_PIN_OUTPUT);
}

static void __init omap3_beagle_config_mcspi4_mux(void)
{
	/* NOTE: Clock pins need to be in input mode */
	omap_mux_init_signal("mcbsp1_clkr.mcspi4_clk", OMAP_PIN_INPUT);
	omap_mux_init_signal("mcbsp1_dx.mcspi4_simo", OMAP_PIN_OUTPUT);
	omap_mux_init_signal("mcbsp1_dr.mcspi4_somi", OMAP_PIN_INPUT_PULLUP);
	omap_mux_init_signal("mcbsp1_fsx.mcspi4_cs0", OMAP_PIN_OUTPUT);
}

static void __init omap3_beagle_config_mcbsp3_mux(void)
{
	omap_mux_init_signal("mcbsp3_fsx.uart2_rx", OMAP_PIN_INPUT);
	omap_mux_init_signal("uart2_cts.mcbsp3_dx", OMAP_PIN_OUTPUT);
	omap_mux_init_signal("uart2_rts.mcbsp3_dr", OMAP_PIN_INPUT);
	/* NOTE: Clock pins need to be in input mode */
	omap_mux_init_signal("uart2_tx.mcbsp3_clkx", OMAP_PIN_INPUT);
}

static void __init omap3_beagle_config_fpga_mux(void)
{
	omap3_beagle_config_mcbsp3_mux();
	omap3_beagle_config_mcspi3_mux();
	omap3_beagle_config_mcspi4_mux();
}

////////////////////////////// ADDED BVL /////////////////////////////////////////////////
#define SPI0_BASE 0

static struct resource omap3_spi_resource[] = {
	{
		.start = SPI0_BASE,
		.end = SPI0_BASE + 256-1,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device omap3_spi_device = {
	.name = "omap3_spi",
	.id = 0,
	.num_resources = ARRAY_SIZE(omap3_spi_resource),
	.resource = omap3_spi_resource,
};

static struct spi_board_info omap3_spi_devices[] = {
	//CS 0
	{
		.modalias = "omap3_spi",
		.max_speed_hz = 48000000,
		//.platform_data =
		.bus_num = 3,
		.chip_select = 0,
		.mode = SPI_MODE_1,
	},

	//CS 1
	{
		.modalias = "omap3_spi",
		.max_speed_hz = 48000000,
		//.platform_data =
		.bus_num = 3,
		.chip_select = 1,
		.mode = SPI_MODE_1,
	},
};

/////////////////////////////////////////////// END ////////////////////////////////////////

/*
static struct spi_board_info beagle_mcspi_board_info[] = {
	/* spi 3.0
	{
		.modalias	= "spidev",
		.max_speed_hz	= 48000000, //48 Mbps
		.bus_num	= 3,
		.chip_select	= 0,
		.mode = SPI_MODE_1,
	},
	/* spi 3.1
	{
		.modalias	= "spidev",
		.max_speed_hz	= 48000000, //48 Mbps
		.bus_num	= 3,
		.chip_select	= 1,
		.mode = SPI_MODE_1,
	},
	/* spi 4.0
	{
		.modalias	= "spidev",
		.max_speed_hz	= 48000000, //48 Mbps
		.bus_num	= 4,
		.chip_select	= 0,
		.mode = SPI_MODE_1,
	},
};*/

static void __init omap3_beagle_init(void)
{
	omap3_mux_init(board_mux, OMAP_PACKAGE_CBB);
	omap3_beagle_init_rev();

	if ((!strcmp(expansionboard_name, "zippy")) || (!strcmp(expansionboard_name, "zippy2")))
	{
		pr_info("Beagle expansionboard: initializing zippy mmc\n");
		platform_device_register(&omap_zippy_device);

		expansion_config.i2c_settings = EXPANSION_I2C_ZIPPY;
		expansion_config.mmc_settings = EXPANSION_MMC_ZIPPY;

		omap_mux_init_gpio(OMAP3BEAGLE_GPIO_ZIPPY_MMC_WP, OMAP_PIN_INPUT);
		omap_mux_init_gpio(OMAP3BEAGLE_GPIO_ZIPPY_MMC_CD, OMAP_PIN_INPUT);
	}

	if ((!strcmp(expansionboard_name, "bbtoys-wifi")) || (!strcmp(expansionboard_name, "lsr-com6l-adpt")))
	{
	#if defined(CONFIG_WL12XX) || defined(CONFIG_WL12XX_MODULE)
		pr_info("Beagle expansion: wl12xx: setting up gpio pinmux\n");

		omap_mux_init_gpio(OMAP_BEAGLE_FM_EN_BT_WU, OMAP_PIN_OUTPUT);
		omap_mux_init_gpio(OMAP_BEAGLE_BT_EN_GPIO, OMAP_PIN_OUTPUT);
		omap_mux_init_gpio(OMAP_BEAGLE_WLAN_EN_GPIO, OMAP_PIN_OUTPUT);

		omap_mux_init_gpio(OMAP_BEAGLE_WLAN_IRQ_GPIO, OMAP_PIN_INPUT_PULLUP);

		/* WLAN SDIO: MMC2 CLK */
		omap_mux_init_signal("sdmmc2_clk.sdmmc2_clk", OMAP_PIN_INPUT_PULLUP);

		/* WLAN SDIO: MMC2 CMD */
		omap_mux_init_signal("sdmmc2_cmd.sdmmc2_cmd", OMAP_PIN_INPUT_PULLUP);

		/* WLAN SDIO: MMC2 DAT[0-3] */
		omap_mux_init_signal("sdmmc2_dat0.sdmmc2_dat0", OMAP_PIN_INPUT_PULLUP);
		omap_mux_init_signal("sdmmc2_dat1.sdmmc2_dat1", OMAP_PIN_INPUT_PULLUP);
		omap_mux_init_signal("sdmmc2_dat2.sdmmc2_dat2", OMAP_PIN_INPUT_PULLUP);
		omap_mux_init_signal("sdmmc2_dat3.sdmmc2_dat3", OMAP_PIN_INPUT_PULLUP);

		expansion_config.mmc_settings = EXPANSION_MMC_WIFI;
		expansion_config.i2c_settings = EXPANSION_I2C_COM6L;
	#endif
	}

	if (!strcmp(expansionboard2_name, "bbtoys-ulcd"))
	{
		int r;
		expansion_config.i2c_settings = EXPANSION_I2C_7ULCD;

		/* TODO: set lcd_driver_name by command line or device tree */
		beagle_config.lcd_driver_name = "tfc_s9700rtwv35tr-01b",
		lcd_panel.name = beagle_config.lcd_driver_name;

		r = gpio_request_one(beagle_config.lcd_pwren, GPIOF_OUT_INIT_LOW, "LCD power");
		if (r < 0)
			pr_err("Beagle expansionboard: Unable to get LCD power enable GPIO\n");
	}

	if (gpio_is_valid(beagle_config.mmc1_gpio_wp))
		omap_mux_init_gpio(beagle_config.mmc1_gpio_wp, OMAP_PIN_INPUT);

	switch (expansion_config.mmc_settings) {
	case EXPANSION_MMC_WIFI:
		mmcbbt[0].caps = beagle_config.mmc_caps;
		omap_hsmmc_init(mmcbbt);
		break;
	case EXPANSION_MMC_ZIPPY:
		mmc_zippy[0].caps = beagle_config.mmc_caps;
		omap_hsmmc_init(mmc_zippy);
		break;
	default:
		mmc[0].caps = beagle_config.mmc_caps;
		omap_hsmmc_init(mmc);
	}

	omap3_beagle_i2c_init();

	gpio_buttons[0].gpio = beagle_config.usr_button_gpio;

	platform_add_devices(omap3_beagle_devices,
			ARRAY_SIZE(omap3_beagle_devices));
	if (gpio_is_valid(beagle_config.dvi_pd_gpio))
		omap_mux_init_gpio(beagle_config.dvi_pd_gpio, OMAP_PIN_OUTPUT);
	omap_display_init(&beagle_dss_data);
	omap_serial_init();
	omap_sdrc_init(mt46h32m32lf6_sdrc_params,
				  mt46h32m32lf6_sdrc_params);

	if (!strcmp(expansionboard_name, "zippy"))
	{
		pr_info("Beagle expansionboard: initializing enc28j60\n");
		omap3beagle_enc28j60_init();
	}

	if (!strcmp(expansionboard_name, "zippy2"))
	{
		pr_info("Beagle expansionboard: initializing ks_8851\n");
		omap3beagle_ks8851_init();
	}

	if (!strcmp(expansionboard_name, "trainer"))
	{
		pr_info("Beagle expansionboard: exporting GPIOs 130-141,162 to userspace\n");
		gpio_request(130, "sysfs");
		gpio_export(130, 1);
		gpio_request(131, "sysfs");
		gpio_export(131, 1);
		gpio_request(132, "sysfs");
		gpio_export(132, 1);
		gpio_request(133, "sysfs");
		gpio_export(133, 1);
		gpio_request(134, "sysfs");
		gpio_export(134, 1);
		gpio_request(135, "sysfs");
		gpio_export(135, 1);
		gpio_request(136, "sysfs");
		gpio_export(136, 1);
		gpio_request(137, "sysfs");
		gpio_export(137, 1);
		gpio_request(138, "sysfs");
		gpio_export(138, 1);
		gpio_request(139, "sysfs");
		gpio_export(139, 1);
		gpio_request(140, "sysfs");
		gpio_export(140, 1);
		gpio_request(141, "sysfs");
		gpio_export(141, 1);
		gpio_request(162, "sysfs");
		gpio_export(162, 1);
	}

	if ((!strcmp(expansionboard_name, "bbtoys-wifi")) || (!strcmp(expansionboard_name, "lsr-com6l-adpt")))
	{
	#if defined(CONFIG_WL12XX) || defined(CONFIG_WL12XX_MODULE)
		pr_info("Beagle expansionboard: initializing wl12xx platform\n");

		if (!strcmp(wl12xx_name, "wl12xx_26mhz")) {
			pr_info("wl12xx: 26Mhz reference clock (TiWi5)\n");
			omap_beagle_wlan_data_26mhz.irq = gpio_to_irq(OMAP_BEAGLE_WLAN_IRQ_GPIO);
			if (wl12xx_set_platform_data(&omap_beagle_wlan_data_26mhz))
				pr_err("error setting wl12xx data\n");
		} else {
			pr_info("wl12xx: 38.4Mhz reference clock (TiWi2/TiWi-BLE)\n");
			pr_info("wl12xx: for (TiWi5) support pass kernel [wl12xx_clk=wl12xx_26mhz]\n");
			omap_beagle_wlan_data.irq = gpio_to_irq(OMAP_BEAGLE_WLAN_IRQ_GPIO);
			if (wl12xx_set_platform_data(&omap_beagle_wlan_data))
				pr_err("error setting wl12xx data\n");
		}

		pr_info("Beagle expansionboard: registering wl12xx bt platform device\n");
		platform_device_register(&wl12xx_device);
		platform_device_register(&btwilink_device);
		pr_info("Beagle expansionboard: registering wl12xx wifi platform device\n");
		platform_device_register(&omap_vwlan_device);
	#endif
	}
	platform_device_register(&omap3_spi_device);
	if (!strcmp(expansionboard_name, "beaglefpga"))
	{
		pr_info("Beagle expansionboard: enabling SPIdev for McSPI3/4 and pin muxing for McBSP3 slave mode\n");

		/* FPGA pin settings configure McSPI 3, McSPI 4 and McBSP 3 */
		omap3_beagle_config_fpga_mux();

		/* register McSPI 3 and McSPI 4 for FPGA programming and control */
//		spi_register_board_info(beagle_mcspi_board_info, ARRAY_SIZE(beagle_mcspi_board_info)); CHANGED BVL
	}
//CHANGED BVL
	/*if (!strcmp(expansionboard_name, "spidev"))
	{
		pr_info("Beagle expansionboard: registering spidev\n");
		omap3_beagle_config_mcspi3_mux();
		omap3_beagle_config_mcspi4_mux();
		spi_register_board_info(beagle_mcspi_board_info, ARRAY_SIZE(beagle_mcspi_board_info));
	}*/
///////////////////////////////// ADDED BVL //////////////////////////////////////////////////
	omap3_beagle_config_mcspi3_mux();
	omap3_beagle_config_mcspi4_mux();
	spi_register_board_info(omap3_spi_devices,ARRAY_SIZE(omap3_spi_devices));
///////////////////////////////// END ADDED //////////////////////////////////////////////////
	if (!strcmp(expansionboard2_name, "bbtoys-ulcd"))
	{
	#if defined(CONFIG_TOUCHSCREEN_TSC2007) || defined(CONFIG_TOUCHSCREEN_TSC2007_MODULE)
		pr_info("Beagle expansionboard: initializing touchscreen: tsc2007\n");
		omap3beagle_tsc2007_init();
	#endif
	}

	usb_bind_phy("musb-hdrc.0.auto", 0, "twl4030_usb");
	usb_musb_init(NULL);
	usbhs_init(&usbhs_bdata);
	board_nand_init(omap3beagle_nand_partitions,
			ARRAY_SIZE(omap3beagle_nand_partitions), NAND_CS,
			NAND_BUSWIDTH_16, NULL);
	omap_twl4030_audio_init("omap3beagle", NULL);

	/* Ensure msecure is mux'd to be able to set the RTC. */
	omap_mux_init_signal("sys_drm_msecure", OMAP_PIN_OFF_OUTPUT_HIGH);

	/* Ensure SDRC pins are mux'd for self-refresh */
	omap_mux_init_signal("sdrc_cke0", OMAP_PIN_OUTPUT);
	omap_mux_init_signal("sdrc_cke1", OMAP_PIN_OUTPUT);

	pwm_add_table(pwm_lookup, ARRAY_SIZE(pwm_lookup));
}

static int __init omap3_beagle_late_initcall(void)
{
	if (!machine_is_omap3_beagle())
		return 0;

	if (!cpu_is_omap3630())
		return 0;

#if defined(CONFIG_VIDEO_MT9P031)
	if ((!strcmp(camera_name, "lbcm5m1")) || (!strcmp(camera_name, "li5m03")))
	{
		pr_info("Beagle camera: MT9P031 init\n");

		reg_1v8 = regulator_get(NULL, "cam_1v8");
		if (IS_ERR(reg_1v8))
			pr_err("%s: cannot get cam_1v8 regulator\n", __func__);
		else
			regulator_enable(reg_1v8);

		reg_2v8 = regulator_get(NULL, "cam_2v8");
		if (IS_ERR(reg_2v8))
			pr_err("%s: cannot get cam_2v8 regulator\n", __func__);
		else
			regulator_enable(reg_2v8);

		omap3_init_camera(&beagle_isp_platform_data);
	}
#endif
	return 0;
}

early_param("buddy", expansionboard_setup);
early_param("buddy2", expansionboard2_setup);
early_param("camera", camera_setup);
early_param("wl12xx_clk", wl12xx_setup);

late_initcall(omap3_beagle_late_initcall);

MACHINE_START(OMAP3_BEAGLE, "OMAP3 Beagle Board")
	/* Maintainer: Syed Mohammed Khasim - http://beagleboard.org */
	.atag_offset	= 0x100,
	.reserve	= omap_reserve,
	.map_io		= omap3_map_io,
	.init_early	= omap3_init_early,
	.init_irq	= omap3_init_irq,
	.handle_irq	= omap3_intc_handle_irq,
	.init_machine	= omap3_beagle_init,
	.init_late	= omap3_init_late,
	.init_time	= omap3_secure_sync32k_timer_init,
	.restart	= omap3xxx_restart,
MACHINE_END
