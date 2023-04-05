/*
 * arch/arm/mach-tegra/board-surface-rt-sensors.c
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
#include <linux/input/cap1106.h>
#include <linux/input/lid.h>
#include <linux/i2c.h>
#include <linux/nct1008.h>
#include <linux/mpu.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>
#include <mach/edp.h>
#include <asm/mach-types.h>
#include "board-surface-rt.h"
#include "../cpu-tegra.h"

#include <mach/pinmux-tegra30.h>

static struct i2c_board_info surface_rt_i2c4_battery_charger_info[] = {
	{
		I2C_BOARD_INFO("surface-rt-ec", 0x0A),
	},
};




static struct throttle_table tj_throttle_table[] = {
		/* CPU_THROT_LOW cannot be used by other than CPU */
		/* NO_CAP cannot be used by CPU */
		/*    CPU,    CBUS,    SCLK,     EMC */
		{ { 1000000,  NO_CAP,  NO_CAP,  NO_CAP } },
		{ {  760000,  NO_CAP,  NO_CAP,  NO_CAP } },
		{ {  760000,  NO_CAP,  NO_CAP,  NO_CAP } },
		{ {  620000,  NO_CAP,  NO_CAP,  NO_CAP } },
		{ {  620000,  NO_CAP,  NO_CAP,  NO_CAP } },
		{ {  620000,  437000,  NO_CAP,  NO_CAP } },
		{ {  620000,  352000,  NO_CAP,  NO_CAP } },
		{ {  475000,  352000,  NO_CAP,  NO_CAP } },
		{ {  475000,  352000,  NO_CAP,  NO_CAP } },
		{ {  475000,  352000,  250000,  375000 } },
		{ {  475000,  352000,  250000,  375000 } },
		{ {  475000,  247000,  204000,  375000 } },
		{ {  475000,  247000,  204000,  204000 } },
		{ {  475000,  247000,  204000,  204000 } },
	  { { CPU_THROT_LOW,  247000,  204000,  102000 } },
};

static struct balanced_throttle tj_throttle = {
	.throt_tab_size = ARRAY_SIZE(tj_throttle_table),
	.throt_tab = tj_throttle_table,
};

static int __init surface_rt_throttle_init(void)
{
	if (machine_is_surface_rt())
		balanced_throttle_register(&tj_throttle, "tegra-balanced");
	return 0;
}
module_init(surface_rt_throttle_init);

static struct nct1008_platform_data surface_rt_nct1008_pdata = {
	.supported_hwrev = true,
	.ext_range = true,
	.conv_rate = 0x08, /* 0x08 corresponds to 63Hz conversion rate */
	.offset = 8, /* 4 * 2C. 1C for device accuracies */

	.shutdown_ext_limit = 90, /* C */
	.shutdown_local_limit = 100, /* C */

	.num_trips = 1,
	.trips = {
		/* Thermal Throttling */
		[0] = {
			.cdev_type = "tegra-balanced",
			.trip_temp = 80000,
			.trip_type = THERMAL_TRIP_PASSIVE,
			.upper = THERMAL_NO_LIMIT,
			.lower = THERMAL_NO_LIMIT,
			.hysteresis = 0,
		},
	},
};

static struct i2c_board_info surface_rt_i2c4_nct1008_board_info[] = {
	{
		I2C_BOARD_INFO("nct1008", 0x4C),
		.platform_data = &surface_rt_nct1008_pdata,
		.irq = -1,
	}
};

static int surface_rt_nct1008_init(void)
{
	int ret = 0;

	/* FIXME: enable irq when throttling is supported */
	surface_rt_i2c4_nct1008_board_info[0].irq =
				gpio_to_irq(SURFACE_RT_TEMP_ALERT_GPIO);

	ret = gpio_request(SURFACE_RT_TEMP_ALERT_GPIO, "temp_alert");
	if (ret < 0) {
		pr_err("%s: gpio_request failed\n", __func__);
		return ret;
	}

	ret = gpio_direction_input(SURFACE_RT_TEMP_ALERT_GPIO);
	if (ret < 0) {
		pr_err("%s: set gpio to input failed\n", __func__);
		gpio_free(SURFACE_RT_TEMP_ALERT_GPIO);
	}

	tegra_platform_edp_init(surface_rt_nct1008_pdata.trips,
				&surface_rt_nct1008_pdata.num_trips,
				0); /* edp temperature margin */

	return ret;
}

int __init surface_rt_sensors_init(void)
{
	int err;

	err = surface_rt_nct1008_init();
	if (err)
		pr_err("%s: nct1008 init failed\n", __func__);
	else
		i2c_register_board_info(4, surface_rt_i2c4_nct1008_board_info,
			ARRAY_SIZE(surface_rt_i2c4_nct1008_board_info));

	i2c_register_board_info(4, surface_rt_i2c4_battery_charger_info,
			ARRAY_SIZE(surface_rt_i2c4_battery_charger_info));

	return 0;
}
