/*
 * arch/arm/mach-tegra/board-roth.h
 *
 * Copyright (c) 2012 - 2013, NVIDIA Corporation. All rights reserved.
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

#ifndef _MACH_TEGRA_BOARD_ROTH_H
#define _MACH_TEGRA_BOARD_ROTH_H

#include <mach/gpio.h>
#include <mach/irqs.h>
#include <linux/mfd/tps65090.h>
#include "gpio-names.h"

#define PMC_WAKE_STATUS 0x14
#define PMC_WAKE2_STATUS 0x168

/* External peripheral act as gpio */
#define TPS65090_TEGRA_IRQ_BASE TEGRA_NR_IRQS
#define TPS65090_TEGRA_IRQ_END	(TPS65090_TEGRA_IRQ_BASE + TPS65090_NUM_IRQ)


#define PALMAS_TEGRA_GPIO_BASE	TEGRA_NR_GPIOS

/* Hall Effect Sensor GPIO */
#define TEGRA_GPIO_HALL		TEGRA_GPIO_PS0

/* Audio-related GPIOs */
#define TEGRA_GPIO_CDC_IRQ		TEGRA_GPIO_PW3
#define TEGRA_GPIO_LDO1_EN		TEGRA_GPIO_PV3
#define TEGRA_GPIO_CODEC1_EN	TEGRA_GPIO_PP3
#define TEGRA_GPIO_CODEC2_EN	TEGRA_GPIO_PP1
#define TEGRA_GPIO_CODEC3_EN	TEGRA_GPIO_PV0

#define TEGRA_GPIO_SPKR_EN		-1
#define TEGRA_GPIO_HP_DET		TEGRA_GPIO_PR7
#define TEGRA_GPIO_INT_MIC_EN		-1
#define TEGRA_GPIO_EXT_MIC_EN		-1

#define TEGRA_GPIO_W_DISABLE		TEGRA_GPIO_PDD7
#define TEGRA_GPIO_MODEM_RSVD1		TEGRA_GPIO_PV0
#define TEGRA_GPIO_MODEM_RSVD2		TEGRA_GPIO_PH7

/* External peripheral act as interrupt controller */
/* MAX77663 IRQs */
#define PALMAS_TEGRA_IRQ_BASE   TEGRA_NR_IRQS


/* I2C related GPIOs */
#define TEGRA_GPIO_I2C1_SCL		TEGRA_GPIO_PC4
#define TEGRA_GPIO_I2C1_SDA             TEGRA_GPIO_PC5
#define TEGRA_GPIO_I2C2_SCL             TEGRA_GPIO_PT5
#define TEGRA_GPIO_I2C2_SDA             TEGRA_GPIO_PT6
#define TEGRA_GPIO_I2C3_SCL             TEGRA_GPIO_PBB1
#define TEGRA_GPIO_I2C3_SDA             TEGRA_GPIO_PBB2
#define TEGRA_GPIO_I2C4_SCL             TEGRA_GPIO_PV4
#define TEGRA_GPIO_I2C4_SDA             TEGRA_GPIO_PV5
#define TEGRA_GPIO_I2C5_SCL             TEGRA_GPIO_PZ6
#define TEGRA_GPIO_I2C5_SDA             TEGRA_GPIO_PZ7



int roth_regulator_init(void);
int roth_suspend_init(void);
int roth_sdhci_init(void);
int roth_pinmux_init(void);
int roth_sensors_init(void);
int roth_emc_init(void);
int roth_edp_init(void);
int roth_panel_init(int board_id);
int roth_kbc_init(void);
int roth_pmon_init(void);
int roth_soctherm_init(void);
int roth_fan_init(void);
void __init surface_rt_i2c_hid_init(void);
#endif
