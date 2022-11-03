/*
 * arch/arm/mach-tegra/board-surface-rt.h
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

#ifndef _MACH_TEGRA_BOARD_SURFACE_RT_H
#define _MACH_TEGRA_BOARD_SURFACE_RT_H

#include <mach/irqs.h>
#include "../gpio-names.h"

/* External peripheral act as gpio */
/* TPS6591x GPIOs */
#define TPS6591X_GPIO_BASE		TEGRA_NR_GPIOS
#define TPS6591X_GPIO_0		(TPS6591X_GPIO_BASE + TPS6591X_GPIO_GP0)
#define TPS6591X_GPIO_6		(TPS6591X_GPIO_BASE + TPS6591X_GPIO_GP6)
#define TPS6591X_GPIO_7		(TPS6591X_GPIO_BASE + TPS6591X_GPIO_GP7)
#define TPS6591X_GPIO_8		(TPS6591X_GPIO_BASE + TPS6591X_GPIO_GP8)

/* Audio-related GPIOs */
#define TEGRA_GPIO_CDC_IRQ		TEGRA_GPIO_PW3
#define TEGRA_GPIO_SPKR_EN		-1
#define TEGRA_GPIO_HP_DET		TEGRA_GPIO_PW2
#define TEGRA_GPIO_INT_MIC_EN		TEGRA_GPIO_PK3
#define TEGRA_GPIO_EXT_MIC_EN		TEGRA_GPIO_PK4

/* TPS6591x IRQs */
#define TPS6591X_IRQ_BASE	TEGRA_NR_IRQS

/* UART port which is used by bluetooth */
#define BLUETOOTH_UART_DEV_NAME "/dev/ttyHS2"


int __init surface_rt_sdhci_init(void);
int __init surface_rt_pinmux_init(void);
void __init surface_rt_pinmux_init_early(void);
int __init surface_rt_panel_init(void);
int __init surface_rt_sensors_init(void);
int __init surface_rt_keys_init(void);
int __init surface_rt_pins_state_init(void);
int __init surface_rt_emc_init(void);
int __init touch_init_synaptics_surface_rt(void);
int __init surface_rt_edp_init(void);
int __init surface_rt_regulator_init(void);
void __init surface_rt_tps6591x_regulator_init(void);
int __init surface_rt_suspend_init(void);
void __init surface_rt_tsensor_init(void);
void __init surface_rt_i2c_hid_init(void);


#define SURFACE_RT_TEMP_ALERT_GPIO		TEGRA_GPIO_PS3

#define EN_HSIC_GPIO			TEGRA_GPIO_PR7

#endif
