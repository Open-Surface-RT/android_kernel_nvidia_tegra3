/*
 * arch/arm/mach-tegra/board-surface-rt-i2c-hid.c
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
#include "board.h"
#include "board-common.h"
#include "clock.h"
#include "devices.h"
#include "gpio-names.h"
#include "fuse.h"
#include "pm.h"
#include "wdt-recovery.h"
#include "common.h"
#include "board-roth.h"
#include "gpio-names.h"
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/i2c/i2c-hid.h>

static struct i2c_hid_platform_data i2c_hid_ts_platform_data = {
      .hid_descriptor_address = 0x0000,
};

static struct i2c_board_info i2c_hid_ts[] ={
	{
		I2C_BOARD_INFO("hid", 0x4b),
		.platform_data = &i2c_hid_ts_platform_data,
		.irq = -1,
	},
};

static struct i2c_hid_platform_data i2c_hid_tc_platform_data = {
      .hid_descriptor_address = 0x0041,
};

static struct i2c_board_info i2c_hid_tc[] ={
	{
		I2C_BOARD_INFO("hid", 0x00),
		.platform_data = &i2c_hid_tc_platform_data,
		.irq = -1,
	},
};

void __init surface_rt_i2c_hid_init(void)
{

	gpio_request(TEGRA_GPIO_PO6, "touch-irq");
	gpio_direction_input(TEGRA_GPIO_PO6);
	i2c_hid_ts[0].irq = gpio_to_irq(TEGRA_GPIO_PO6);
	i2c_register_board_info(1, i2c_hid_ts, 1);

	gpio_request(TEGRA_GPIO_PC7, "tcover-irq");
	gpio_direction_input(TEGRA_GPIO_PC7);
	i2c_hid_tc[0].irq = gpio_to_irq(TEGRA_GPIO_PC7);
	i2c_register_board_info(0, i2c_hid_tc, 1);
	
	/* HID SENSORS GO HERE TOO */

}
