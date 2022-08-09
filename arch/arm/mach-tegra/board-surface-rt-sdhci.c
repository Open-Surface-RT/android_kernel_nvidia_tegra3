/*
 * arch/arm/mach-tegra/board-surface-rt-sdhci.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (C) 2012 NVIDIA Corporation.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <mach/iomap.h>
#include <mach/sdhci.h>

#include "gpio-names.h"
#include "board-surface-rt.h"
#include <mach/pinmux.h>
#include <mach/pinmux-tegra30.h>



#include <mach/gpio-tegra.h>
#include <mach/io_dpd.h>
#include <mach/irqs.h>

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mmc/host.h>
#include <linux/wlan_plat.h>
#include <linux/resource.h>


#define TEGRA_WLAN_PWR	TEGRA_GPIO_PD4
#define TEGRA_WLAN_RST	TEGRA_GPIO_PD3
#define TEGRA_WLAN_WOW	TEGRA_GPIO_PO4

static void (*wifi_status_cb)(int card_present, void *dev_id);
static void *wifi_status_cb_devid;
static int tegra_wifi_status_register(void (*callback)(int , void *), void *);


static int tegra_wifi_reset(int on);
static int tegra_wifi_power(int on);
static int tegra_wifi_set_carddetect(int val);

static struct wifi_platform_data tegra_wifi_control = {
	.set_power      = tegra_wifi_power,
	.set_reset      = tegra_wifi_reset,
	.set_carddetect = tegra_wifi_set_carddetect,
};

static void set_pin_pupd_input(int pin , int pupd , int input)
{
	int err;

	err = tegra_pinmux_set_pullupdown(pin , pupd);
	if (err < 0)
		printk(KERN_ERR "%s: can't set pin %d pullupdown to %d\n", __func__, pin , pupd);

	err = tegra_pinmux_set_io(pin , input);
	if (err < 0)
		printk(KERN_ERR "%s: can't set pin %d e_input to %d\n", __func__, pin , input);
}

static int wifi_sdio_gpio[] = {
	TEGRA_GPIO_PA6,
	TEGRA_GPIO_PA7,
	TEGRA_GPIO_PB7,
	TEGRA_GPIO_PB6,
	TEGRA_GPIO_PB5,
	TEGRA_GPIO_PB4,
};

static int enable_wifi_sdio_func(void)
{
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(wifi_sdio_gpio); i++) {
		gpio_free(wifi_sdio_gpio[i]);
	}

	set_pin_pupd_input(TEGRA_PINGROUP_SDMMC3_CLK , TEGRA_PUPD_NORMAL , TEGRA_PIN_INPUT);
	set_pin_pupd_input(TEGRA_PINGROUP_SDMMC3_CMD , TEGRA_PUPD_PULL_UP, TEGRA_PIN_INPUT);
	set_pin_pupd_input(TEGRA_PINGROUP_SDMMC3_DAT3 , TEGRA_PUPD_PULL_UP , TEGRA_PIN_INPUT);
	set_pin_pupd_input(TEGRA_PINGROUP_SDMMC3_DAT2 , TEGRA_PUPD_PULL_UP , TEGRA_PIN_INPUT);
	set_pin_pupd_input(TEGRA_PINGROUP_SDMMC3_DAT1 , TEGRA_PUPD_PULL_UP , TEGRA_PIN_INPUT);
	set_pin_pupd_input(TEGRA_PINGROUP_SDMMC3_DAT0 , TEGRA_PUPD_PULL_UP , TEGRA_PIN_INPUT);

	return 0;
}

static int disable_wifi_sdio_func(void)
{
	unsigned int rc = 0;
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(wifi_sdio_gpio); i++) {
		rc = gpio_request(wifi_sdio_gpio[i], NULL);
		if (rc) {
			printk(KERN_INFO "%s, request gpio %d failed !!!\n", __func__, wifi_sdio_gpio[i]);
			return rc;
		}

		rc = gpio_direction_output(wifi_sdio_gpio[i], 0);
		if (rc) {
			printk(KERN_INFO "%s, direction gpio %d failed !!!\n", __func__, wifi_sdio_gpio[i]);
			return rc;
		}
	}

	set_pin_pupd_input(TEGRA_PINGROUP_SDMMC3_CLK , TEGRA_PUPD_NORMAL , TEGRA_PIN_OUTPUT);
	set_pin_pupd_input(TEGRA_PINGROUP_SDMMC3_CMD , TEGRA_PUPD_NORMAL , TEGRA_PIN_OUTPUT);
	set_pin_pupd_input(TEGRA_PINGROUP_SDMMC3_DAT3 , TEGRA_PUPD_NORMAL , TEGRA_PIN_OUTPUT);
	set_pin_pupd_input(TEGRA_PINGROUP_SDMMC3_DAT2 , TEGRA_PUPD_NORMAL , TEGRA_PIN_OUTPUT);
	set_pin_pupd_input(TEGRA_PINGROUP_SDMMC3_DAT1 , TEGRA_PUPD_NORMAL , TEGRA_PIN_OUTPUT);
	set_pin_pupd_input(TEGRA_PINGROUP_SDMMC3_DAT0 , TEGRA_PUPD_NORMAL , TEGRA_PIN_OUTPUT);
	return 0;
}


static int tegra_wifi_power(int on)
{


	pr_debug("%s: %d\n", __func__, on);



	if (on)
	    gpio_direction_input(TEGRA_WLAN_WOW);
	else
	    gpio_direction_output(TEGRA_WLAN_WOW, 0);

	if (on) {
	
	
		enable_wifi_sdio_func();
		if (!gpio_get_value(TEGRA_WLAN_PWR)) {
			gpio_set_value(TEGRA_WLAN_PWR, 1);
		}
	}
	mdelay(100);
	gpio_set_value(TEGRA_WLAN_RST, on);
	mdelay(200);

	
	if (!on) {
	
	
		disable_wifi_sdio_func();
	}


	return 0;
}

static int tegra_wifi_reset(int on)
{
	pr_debug("%s: do nothing\n", __func__);
	return 0;
}

static int tegra_wifi_set_carddetect(int val)
{
	pr_debug("%s: %d\n", __func__, val);
	if (wifi_status_cb)
		wifi_status_cb(val, wifi_status_cb_devid);
	else
		pr_warning("%s: Nobody to notify\n", __func__);
	return 0;
}

static int tegra_wifi_status_register(
		void (*callback)(int card_present, void *dev_id),
		void *dev_id)
{
	if (wifi_status_cb)
		return -EAGAIN;
	wifi_status_cb = callback;
	wifi_status_cb_devid = dev_id;
	return 0;
}

#ifdef CONFIG_TEGRA_PREPOWER_WIFI
static int __init tegra_wifi_prepower(void)
{


	tegra_wifi_power(1);

	return 0;
}
subsys_initcall_sync(tegra_wifi_prepower);
#endif

static struct resource wifi_resource[] = {
	[0] = {
		.name	= "mwifiex_sdio_irq",
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL | IORESOURCE_IRQ_SHAREABLE,
	},
};

static struct platform_device tegra_mrvl_wifi_device = {
	.name		= "mwifiex_sdio",
	.id		= 1,
	.num_resources	= 1,
	.resource	= wifi_resource,
	.dev		= {
	.platform_data = &tegra_wifi_control,
	},
};



static struct resource sdhci_resource0[] = {
	[0] = {
		.start  = INT_SDMMC1,
		.end    = INT_SDMMC1,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SDMMC1_BASE,
		.end	= TEGRA_SDMMC1_BASE + TEGRA_SDMMC1_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource sdhci_resource2[] = {
	[0] = {
		.start  = INT_SDMMC3,
		.end    = INT_SDMMC3,
		.flags  = IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SDMMC3_BASE,
		.end	= TEGRA_SDMMC3_BASE + TEGRA_SDMMC3_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource sdhci_resource3[] = {
	[0] = {
		.start	= INT_SDMMC4,
		.end	= INT_SDMMC4,
		.flags	= IORESOURCE_IRQ,
	},
	[1] = {
		.start	= TEGRA_SDMMC4_BASE,
		.end	= TEGRA_SDMMC4_BASE + TEGRA_SDMMC4_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct tegra_sdhci_platform_data tegra_sdhci_platform_data0 = {
	.cd_gpio = TEGRA_GPIO_PI5,
	.wp_gpio = -1,
	.power_gpio = TEGRA_GPIO_PD7,
	.tap_delay = 0x0F,
	.ddr_clk_limit = 41000000,
/*	.is_voltage_switch_supported = true,
	.vdd_rail_name = "vddio_sdmmc1",
	.slot_rail_name = "vddio_sd_slot",
	.vdd_max_uv = 3320000,
	.vdd_min_uv = 3280000,
	.max_clk = 208000000,
	.is_8bit_supported = false, */
};


static struct tegra_sdhci_platform_data tegra_sdhci_platform_data2 = {
	.cd_gpio = -1,
	.wp_gpio = -1,
	.power_gpio = -1,
	.tap_delay = 0x0F,
	.ddr_clk_limit = 41000000,
	.mmc_data = {
		.register_status_notify	= tegra_wifi_status_register,
		.built_in = 0,
	},
};


static struct tegra_sdhci_platform_data tegra_sdhci_platform_data3 = {
	.cd_gpio = -1,
	.wp_gpio = -1,
	.power_gpio = -1,
	.is_8bit = 1,
	.tap_delay = 0x0F,
	.ddr_clk_limit = 41000000,
	.mmc_data = {
		.built_in = 1,
		.ocr_mask = MMC_OCR_1V8_MASK,
	}
};





static struct platform_device tegra_sdhci_device0 = {
	.name		= "sdhci-tegra",
	.id		= 0,
	.resource	= sdhci_resource0,
	.num_resources	= ARRAY_SIZE(sdhci_resource0),
	.dev = {
		.platform_data = &tegra_sdhci_platform_data0,
	},
};

static struct platform_device tegra_sdhci_device2 = {
	.name		= "sdhci-tegra",
	.id		= 2,
	.resource	= sdhci_resource2,
	.num_resources	= ARRAY_SIZE(sdhci_resource2),
	.dev = {
		.platform_data = &tegra_sdhci_platform_data2,
	},
};

static struct platform_device tegra_sdhci_device3 = {
	.name		= "sdhci-tegra",
	.id		= 3,
	.resource	= sdhci_resource3,
	.num_resources	= ARRAY_SIZE(sdhci_resource3),
	.dev = {
		.platform_data = &tegra_sdhci_platform_data3,
	},
};

static int __init tegra_wifi_init(void)
{
	int rc;

	rc = gpio_request(TEGRA_WLAN_PWR, "wlan_power");
	if (rc)
		pr_err("WLAN_PWR gpio request failed:%d\n", rc);
	rc = gpio_request(TEGRA_WLAN_RST, "wlan_rst");
	if (rc)
		pr_err("WLAN_RST gpio request failed:%d\n", rc);
	rc = gpio_request(TEGRA_WLAN_WOW, "bcmsdh_sdmmc");
	if (rc)
		pr_err("WLAN_WOW gpio request failed:%d\n", rc);

	rc = gpio_direction_output(TEGRA_WLAN_PWR, 0);
	if (rc)
		pr_err("WLAN_PWR gpio direction configuration failed:%d\n", rc);
	gpio_direction_output(TEGRA_WLAN_RST, 0);
	if (rc)
		pr_err("WLAN_RST gpio direction configuration failed:%d\n", rc);
	rc = gpio_direction_input(TEGRA_WLAN_WOW);
	if (rc)
		pr_err("WLAN_WOW gpio direction configuration failed:%d\n", rc);


	wifi_resource[0].start = wifi_resource[0].end =
	gpio_to_irq(TEGRA_GPIO_PU5);
	platform_device_register(&tegra_mrvl_wifi_device);
	

	return 0;
}


int __init surface_rt_sdhci_init(void)
{
	platform_device_register(&tegra_sdhci_device3);
	platform_device_register(&tegra_sdhci_device0);
	platform_device_register(&tegra_sdhci_device2);
	tegra_wifi_init();
	
	return 0;
}
