/*
 * arch/arm/mach-tegra/board-surface-rt-panel.c
 *
 * Copyright (c) 2012, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/pwm_backlight.h>
#include <linux/nvhost.h>
#include <linux/nvmap.h>
#include <mach/iomap.h>
#include <mach/dc.h>

#include "board.h"
#include "board-surface-rt.h"
#include "devices.h"
#include "tegra3_host1x_devices.h"

/* surface-rt default display board pins */
#define surface_rt_lvds_shutdown	TEGRA_GPIO_PB2

static atomic_t sd_brightness = ATOMIC_INIT(255);

static struct regulator *surface_rt_lvds_vdd_panel;
static struct regulator *surface_rt_lvds_vdd_bl = NULL;

static tegra_dc_bl_output surface_rt_bl_output_measured = {
	0, 4, 4, 4, 4, 5, 6, 7,
	8, 9, 10, 11, 12, 13, 14, 15,
	16, 17, 18, 19, 20, 21, 22, 23,
	24, 25, 26, 27, 28, 29, 30, 31,
	32, 33, 34, 35, 36, 37, 38, 39,
	40, 41, 42, 43, 44, 45, 46, 47,
	48, 49, 49, 50, 51, 52, 53, 54,
	55, 56, 57, 58, 59, 60, 61, 62,
	63, 64, 65, 66, 67, 68, 69, 70,
	70, 72, 73, 74, 75, 76, 77, 78,
	79, 80, 81, 82, 83, 84, 85, 86,
	87, 88, 89, 90, 91, 92, 93, 94,
	95, 96, 97, 98, 99, 100, 101, 102,
	103, 104, 105, 106, 107, 108, 110, 111,
	112, 113, 114, 115, 116, 117, 118, 119,
	120, 121, 122, 123, 124, 124, 125, 126,
	127, 128, 129, 130, 131, 132, 133, 133,
	134, 135, 136, 137, 138, 139, 140, 141,
	142, 143, 144, 145, 146, 147, 148, 148,
	149, 150, 151, 152, 153, 154, 155, 156,
	157, 158, 159, 160, 161, 162, 163, 164,
	165, 166, 167, 168, 169, 170, 171, 172,
	173, 174, 175, 176, 177, 179, 180, 181,
	182, 184, 185, 186, 187, 188, 189, 190,
	191, 192, 193, 194, 195, 196, 197, 198,
	199, 200, 201, 202, 203, 204, 205, 206,
	207, 208, 209, 211, 212, 213, 214, 215,
	216, 217, 218, 219, 220, 221, 222, 223,
	224, 225, 226, 227, 228, 229, 230, 231,
	232, 233, 234, 235, 236, 237, 238, 239,
	240, 241, 242, 243, 244, 245, 246, 247,
	248, 249, 250, 251, 252, 253, 254, 255
};

static p_tegra_dc_bl_output bl_output;

static int surface_rt_backlight_init(struct device *dev)
{
	bl_output = surface_rt_bl_output_measured;

	if (WARN_ON(ARRAY_SIZE(surface_rt_bl_output_measured) != 256))
		pr_err("%s: bl_output array does not have 256 elements\n", __func__);

	return 1;
}

static int surface_rt_backlight_notify(struct device *unused, int brightness)
{
	int cur_sd_brightness = atomic_read(&sd_brightness);

	/* SD brightness is a percentage, 8-bit value. */
	brightness = DIV_ROUND_CLOSEST((brightness * cur_sd_brightness), 255);

	/* Apply any backlight response curve */
	if (brightness > 255)
		pr_info("%s: error: Brightness > 255!\n", __func__);
	else
		brightness = bl_output[brightness];

	return brightness;
}

static int surface_rt_disp1_check_fb(struct device *dev, struct fb_info *info);

static struct platform_pwm_backlight_data surface_rt_backlight_data = {
	.pwm_id		= 0,
	.max_brightness	= 255,
	.dft_brightness	= 40,
	.pwm_period_ns		= 50000,
	.init			= surface_rt_backlight_init,
	.notify		= surface_rt_backlight_notify,
	/* Only toggle backlight on fb blank notifications for disp1 */
	.check_fb		= surface_rt_disp1_check_fb,
};

static struct platform_device surface_rt_backlight_device = {
	.name	= "pwm-backlight",
	.id	= -1,
	.dev	= {
		.platform_data = &surface_rt_backlight_data,
	},
};

static int surface_rt_panel_prepoweroff(void)
{
	gpio_set_value(surface_rt_lvds_shutdown, 0);

	return 0;
}

static int surface_rt_panel_postpoweron(void)
{
	gpio_set_value(surface_rt_lvds_shutdown, 1);

	mdelay(50);

	return 0;
}

static int surface_rt_panel_enable(struct device *dev)
{
	if (surface_rt_lvds_vdd_panel == NULL) {
		surface_rt_lvds_vdd_panel = regulator_get(dev, "vdd_lcd_panel");
		if (WARN_ON(IS_ERR(surface_rt_lvds_vdd_panel)))
			pr_err("%s: couldn't get regulator vdd_lcd_panel: %ld\n",
				__func__, PTR_ERR(surface_rt_lvds_vdd_panel));
		else
			regulator_enable(surface_rt_lvds_vdd_panel);
	}
	
	if (surface_rt_lvds_vdd_bl == NULL) {
		surface_rt_lvds_vdd_bl = regulator_get(dev, "vdd_backlight");
		if (WARN_ON(IS_ERR(surface_rt_lvds_vdd_bl)))
			pr_err("%s: couldn't get regulator vdd_backlight: %ld\n",
				__func__, PTR_ERR(surface_rt_lvds_vdd_bl));
		else
			regulator_enable(surface_rt_lvds_vdd_bl);
	}

	return 0;
}

static int surface_rt_panel_disable(void)
{
	mdelay(5);

	if (surface_rt_lvds_vdd_panel) {
		regulator_disable(surface_rt_lvds_vdd_panel);
		regulator_put(surface_rt_lvds_vdd_panel);
		surface_rt_lvds_vdd_panel = NULL;
	}
	
	if (surface_rt_lvds_vdd_bl) {
		regulator_disable(surface_rt_lvds_vdd_bl);
		regulator_put(surface_rt_lvds_vdd_bl);
		surface_rt_lvds_vdd_bl = NULL;
	}	

	return 0;
}

#ifdef CONFIG_TEGRA_DC
static struct resource surface_rt_disp1_resources[] = {
	{
		.name	= "irq",
		.start	= INT_DISPLAY_GENERAL,
		.end	= INT_DISPLAY_GENERAL,
		.flags	= IORESOURCE_IRQ,
	},
	{
		.name	= "regs",
		.start	= TEGRA_DISPLAY_BASE,
		.end	= TEGRA_DISPLAY_BASE + TEGRA_DISPLAY_SIZE-1,
		.flags	= IORESOURCE_MEM,
	},
	{
		.name	= "fbmem",
		.start	= 0,	/* Filled in by surface_rt_panel_init() */
		.end	= 0,	/* Filled in by surface_rt_panel_init() */
		.flags	= IORESOURCE_MEM,
	},
};
#endif

static struct tegra_dc_mode surface_rt_panel_modes[] = {
	{
		/* 1366x768@60Hz */
		.pclk 		= 71980000,
		.h_ref_to_sync	= 1,
		.v_ref_to_sync	= 1,
		.h_sync_width 	= 14,
		.v_sync_width 	= 1,
		.h_back_porch 	= 106,
		.v_back_porch 	= 6,
		.h_active 	= 1366,
		.v_active 	= 768,
		.h_front_porch	= 56,
		.v_front_porch	= 3,
	},
};


#ifdef CONFIG_TEGRA_DC
static struct tegra_fb_data surface_rt_fb_data = {
	.win			= 0,
	.xres			= 1366,
	.yres			= 768,
	.bits_per_pixel	= 32,
};
#endif

static struct tegra_dc_out surface_rt_disp1_out = {
	.align			= TEGRA_DC_ALIGN_MSB,
	.order			= TEGRA_DC_ORDER_RED_BLUE,
	.parent_clk		= "pll_p",
	.parent_clk_backup 	= "pll_d2_out0",

	.type			= TEGRA_DC_OUT_RGB,
	.depth			= 24,
	//.dither		= TEGRA_DC_ERRDIFF_DITHER,

	.modes			= surface_rt_panel_modes,
	.n_modes		= ARRAY_SIZE(surface_rt_panel_modes),

	.enable		= surface_rt_panel_enable,
	.postpoweron		= surface_rt_panel_postpoweron,
	.prepoweroff		= surface_rt_panel_prepoweroff,
	.disable		= surface_rt_panel_disable,

	.height		= 132,
	.width			= 235,
};

#ifdef CONFIG_TEGRA_DC
static struct tegra_dc_platform_data surface_rt_disp1_pdata = {
	.flags		= TEGRA_DC_FLAG_ENABLED,
	.default_out	= &surface_rt_disp1_out,
	.emc_clk_rate	= 300000000,
	.fb		= &surface_rt_fb_data,
};

static struct platform_device surface_rt_disp1_device = {
	.name		= "tegradc",
	.id		= 0,
	.resource	= surface_rt_disp1_resources,
	.num_resources	= ARRAY_SIZE(surface_rt_disp1_resources),
	.dev = {
		.platform_data = &surface_rt_disp1_pdata,
	},
};

static int surface_rt_disp1_check_fb(struct device *dev, struct fb_info *info)
{
	return info->device == &surface_rt_disp1_device.dev;
}
#else
static int surface_rt_disp1_check_fb(struct device *dev, struct fb_info *info)
{
	return 0;
}
#endif

#ifdef CONFIG_TEGRA_NVMAP
static struct nvmap_platform_carveout surface_rt_carveouts[] = {
	[0] = NVMAP_HEAP_CARVEOUT_IRAM_INIT,
	[1] = {
		.name		= "generic-0",
		.usage_mask	= NVMAP_HEAP_CARVEOUT_GENERIC,
		.base		= 0,	/* Filled in by surface_rt_panel_init() */
		.size		= 0,	/* Filled in by surface_rt_panel_init() */
		.buddy_size	= SZ_32K,
	},
};

static struct nvmap_platform_data surface_rt_nvmap_data = {
	.carveouts	= surface_rt_carveouts,
	.nr_carveouts	= ARRAY_SIZE(surface_rt_carveouts),
};

static struct platform_device surface_rt_nvmap_device = {
	.name	= "tegra-nvmap",
	.id	= -1,
	.dev	= {
		.platform_data = &surface_rt_nvmap_data,
	},
};
#endif

static struct platform_device *surface_rt_gfx_devices[] __initdata = {
#ifdef CONFIG_TEGRA_NVMAP
	&surface_rt_nvmap_device,
#endif
	&tegra_pwfm0_device,
	&surface_rt_backlight_device,
};

int __init surface_rt_panel_init(void)
{
	int err;
	struct resource __maybe_unused *res;
#ifdef CONFIG_TEGRA_GRHOST
	struct platform_device *phost1x;
#endif


#ifdef CONFIG_TEGRA_NVMAP
	surface_rt_carveouts[1].base = tegra_carveout_start;
	surface_rt_carveouts[1].size = tegra_carveout_size;
#endif


	err = platform_add_devices(surface_rt_gfx_devices,
				ARRAY_SIZE(surface_rt_gfx_devices));

#ifdef CONFIG_TEGRA_GRHOST
	phost1x = tegra3_register_host1x_devices();
	if (!phost1x)
		return -EINVAL;
#endif

#if defined(CONFIG_TEGRA_GRHOST) && defined(CONFIG_TEGRA_DC)
	res = platform_get_resource_byname(&surface_rt_disp1_device,
					IORESOURCE_MEM, "fbmem");
	res->start = tegra_fb_start;
	res->end = tegra_fb_start + tegra_fb_size - 1;
#endif

	/* Copy the bootloader fb to the fb. */
	__tegra_move_framebuffer(&surface_rt_nvmap_device,
		tegra_fb_start, tegra_bootloader_fb_start,
				min(tegra_fb_size, tegra_bootloader_fb_size));

#if defined(CONFIG_TEGRA_GRHOST) && defined(CONFIG_TEGRA_DC)
	if (!err) {
		surface_rt_disp1_device.dev.parent = &phost1x->dev;
		err = platform_device_register(&surface_rt_disp1_device);
	}
#endif

#if defined(CONFIG_TEGRA_GRHOST) && defined(CONFIG_TEGRA_NVAVP)
	if (!err) {
		nvavp_device.dev.parent = &phost1x->dev;
		err = platform_device_register(&nvavp_device);
	}
#endif

	return err;
}
