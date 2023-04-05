/*
 * arch/arm/mach-tegra/board-surface-rt-pinmux.c
 *
 * Copyright (C) 2012 NVIDIA Corporation
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

#include <linux/gpio.h>
#include <mach/pinmux-tegra30.h>
#include "board-surface-rt.h"

#define DEFAULT_DRIVE(_name)					\
	{							\
		.pingroup = TEGRA_DRIVE_PINGROUP_##_name,	\
		.hsm = TEGRA_HSM_DISABLE,			\
		.schmitt = TEGRA_SCHMITT_ENABLE,		\
		.drive = TEGRA_DRIVE_DIV_1,			\
		.pull_down = TEGRA_PULL_31,			\
		.pull_up = TEGRA_PULL_31,			\
		.slew_rising = TEGRA_SLEW_SLOWEST,		\
		.slew_falling = TEGRA_SLEW_SLOWEST,		\
	}
/* Setting the drive strength of pins
 * hsm: Enable High speed mode (ENABLE/DISABLE)
 * Schimit: Enable/disable schimit (ENABLE/DISABLE)
 * drive: low power mode (DIV_1, DIV_2, DIV_4, DIV_8)
 * pulldn_drive - drive down (falling edge) - Driver Output Pull-Down drive
 *                strength code. Value from 0 to 31.
 * pullup_drive - drive up (rising edge)  - Driver Output Pull-Up drive
 *                strength code. Value from 0 to 31.
 * pulldn_slew -  Driver Output Pull-Up slew control code  - 2bit code
 *                code 11 is least slewing of signal. code 00 is highest
 *                slewing of the signal.
 *                Value - FASTEST, FAST, SLOW, SLOWEST
 * pullup_slew -  Driver Output Pull-Down slew control code -
 *                code 11 is least slewing of signal. code 00 is highest
 *                slewing of the signal.
 *                Value - FASTEST, FAST, SLOW, SLOWEST
 */
#define SET_DRIVE(_name, _hsm, _schmitt, _drive, _pulldn_drive, _pullup_drive, _pulldn_slew, _pullup_slew) \
	{                                               \
		.pingroup = TEGRA_DRIVE_PINGROUP_##_name,   \
		.hsm = TEGRA_HSM_##_hsm,                    \
		.schmitt = TEGRA_SCHMITT_##_schmitt,        \
		.drive = TEGRA_DRIVE_##_drive,              \
		.pull_down = TEGRA_PULL_##_pulldn_drive,    \
		.pull_up = TEGRA_PULL_##_pullup_drive,		\
		.slew_rising = TEGRA_SLEW_##_pulldn_slew,   \
		.slew_falling = TEGRA_SLEW_##_pullup_slew,	\
	}

/* !!!FIXME!!!! POPULATE THIS TABLE */
static __initdata struct tegra_drive_pingroup_config surface_rt_drive_pinmux[] = {
	/* DEFAULT_DRIVE(<pin_group>), */
	/* SET_DRIVE(ATA, DISABLE, DISABLE, DIV_1, 31, 31, FAST, FAST) */
	SET_DRIVE(DAP2,		DISABLE, ENABLE, DIV_1, 31, 31, FASTEST, FASTEST),

	/* All I2C pins are driven to maximum drive strength */
	/* GEN1 I2C */
	SET_DRIVE(DBG,		DISABLE, ENABLE, DIV_1, 31, 31, FASTEST, FASTEST),

	/* GEN2 I2C */
	SET_DRIVE(AT5,		DISABLE, ENABLE, DIV_1, 31, 31, FASTEST, FASTEST),

	/* CAM I2C */
	SET_DRIVE(GME,		DISABLE, ENABLE, DIV_1, 31, 31, FASTEST, FASTEST),

	/* DDC I2C */
	SET_DRIVE(DDC,		DISABLE, ENABLE, DIV_1, 31, 31, FASTEST, FASTEST),

	/* PWR_I2C */
	SET_DRIVE(AO1,		DISABLE, ENABLE, DIV_1, 31, 31, FASTEST, FASTEST),

	/* UART3 */
	SET_DRIVE(UART3,	DISABLE, ENABLE, DIV_1, 31, 31, FASTEST, FASTEST),

	/* SDMMC1 */
	SET_DRIVE(SDIO1,	DISABLE, DISABLE, DIV_1, 46, 42, FAST, FAST),

	/* SDMMC3 */
	SET_DRIVE(SDIO3,	DISABLE, DISABLE, DIV_1, 46, 42, FAST, FAST),

	/* SDMMC4 */
	SET_DRIVE(GMA,		DISABLE, DISABLE, DIV_1, 9, 9, SLOWEST, SLOWEST),
	SET_DRIVE(GMB,		DISABLE, DISABLE, DIV_1, 9, 9, SLOWEST, SLOWEST),
	SET_DRIVE(GMC,		DISABLE, DISABLE, DIV_1, 9, 9, SLOWEST, SLOWEST),
	SET_DRIVE(GMD,		DISABLE, DISABLE, DIV_1, 9, 9, SLOWEST, SLOWEST),

};

#define DEFAULT_PINMUX(_pingroup, _mux, _pupd, _tri, _io)	\
	{							\
		.pingroup	= TEGRA_PINGROUP_##_pingroup,	\
		.func		= TEGRA_MUX_##_mux,		\
		.pupd		= TEGRA_PUPD_##_pupd,		\
		.tristate	= TEGRA_TRI_##_tri,		\
		.io		= TEGRA_PIN_##_io,		\
		.lock		= TEGRA_PIN_LOCK_DEFAULT,	\
		.od		= TEGRA_PIN_OD_DEFAULT,		\
		.ioreset	= TEGRA_PIN_IO_RESET_DEFAULT,	\
	}

#define I2C_PINMUX(_pingroup, _mux, _pupd, _tri, _io, _lock, _od) \
	{							\
		.pingroup	= TEGRA_PINGROUP_##_pingroup,	\
		.func		= TEGRA_MUX_##_mux,		\
		.pupd		= TEGRA_PUPD_##_pupd,		\
		.tristate	= TEGRA_TRI_##_tri,		\
		.io		= TEGRA_PIN_##_io,		\
		.lock		= TEGRA_PIN_LOCK_##_lock,	\
		.od		= TEGRA_PIN_OD_##_od,		\
		.ioreset	= TEGRA_PIN_IO_RESET_DEFAULT,	\
	}

#define VI_PINMUX(_pingroup, _mux, _pupd, _tri, _io, _lock, _ioreset) \
	{							\
		.pingroup	= TEGRA_PINGROUP_##_pingroup,	\
		.func		= TEGRA_MUX_##_mux,		\
		.pupd		= TEGRA_PUPD_##_pupd,		\
		.tristate	= TEGRA_TRI_##_tri,		\
		.io		= TEGRA_PIN_##_io,		\
		.lock		= TEGRA_PIN_LOCK_##_lock,	\
		.od		= TEGRA_PIN_OD_DEFAULT,		\
		.ioreset	= TEGRA_PIN_IO_RESET_##_ioreset	\
	}

static __initdata struct tegra_pingroup_config surface_rt_pinmux_common[] = {

	
	DEFAULT_PINMUX(ULPI_DATA0,      UARTA,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(ULPI_DATA1,      UARTA,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(ULPI_DATA2,      UARTA,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(ULPI_DATA3,      UARTA,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(ULPI_DATA4,      SPI2,  PULL_UP,   TRISTATE,          INPUT),
	DEFAULT_PINMUX(ULPI_DATA5,      SPI2,  PULL_UP,   TRISTATE,          INPUT),
	DEFAULT_PINMUX(ULPI_DATA6,      SPI2,  PULL_UP,   TRISTATE,          INPUT),
	DEFAULT_PINMUX(ULPI_DATA7,      SPI2,  PULL_UP,   TRISTATE,          INPUT),
	DEFAULT_PINMUX(ULPI_CLK,        SPI1,  NORMAL,    TRISTATE,          INPUT),
	DEFAULT_PINMUX(ULPI_DIR,        SPI1,  NORMAL,    TRISTATE,          INPUT),
	DEFAULT_PINMUX(ULPI_NXT,        SPI1,  NORMAL,    TRISTATE,          INPUT),
	DEFAULT_PINMUX(ULPI_STP,        SPI1,  NORMAL,    TRISTATE,          INPUT),
	DEFAULT_PINMUX(DAP3_FS,         I2S2,  PULL_DOWN, TRISTATE,          INPUT),
	DEFAULT_PINMUX(DAP3_DIN,        I2S2,  PULL_DOWN, TRISTATE,          INPUT),
	DEFAULT_PINMUX(DAP3_DOUT,       I2S2,  PULL_DOWN, TRISTATE,          INPUT),
	DEFAULT_PINMUX(DAP3_SCLK,       I2S2,  PULL_DOWN, TRISTATE,          INPUT),
	DEFAULT_PINMUX(GPIO_PV0,        RSVD,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(GPIO_PV1,        RSVD,  NORMAL,    TRISTATE,         INPUT),
	DEFAULT_PINMUX(SDMMC1_CLK,      SDIO1,  NORMAL,    NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC1_CMD,      SDIO1,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC1_DAT3,     SDIO1,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC1_DAT2,     SDIO1,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC1_DAT1,     SDIO1,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC1_DAT0,     SDIO1,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(GPIO_PV2,        OWR,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(GPIO_PV3,        RSVD3,  PULL_UP,    TRISTATE,       INPUT),
	DEFAULT_PINMUX(CLK2_OUT,        EXTPERIPH2,  PULL_DOWN, TRISTATE,    INPUT),
	DEFAULT_PINMUX(CLK2_REQ,        DAP,  PULL_UP,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(LCD_PWR1,        DISPLAYA, PULL_DOWN, TRISTATE,      OUTPUT),
	DEFAULT_PINMUX(LCD_PWR2,        DISPLAYA, PULL_DOWN, TRISTATE,      OUTPUT),
	DEFAULT_PINMUX(LCD_SDIN,        DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_SDOUT,       DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_WR_N,        DISPLAYA, PULL_UP,   NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_CS0_N,       DISPLAYA, PULL_UP,   NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_DC0,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_SCK,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_PWR0,        DISPLAYA, NORMAL,    NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_PCLK,        DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_DE,          DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_HSYNC,       DISPLAYA, PULL_UP,   NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_VSYNC,       DISPLAYA, PULL_UP,   NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D0,          DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D1,          DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D2,          DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D3,          DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D4,          DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D5,          DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D6,          DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D7,          DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D8,          DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D9,          DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D10,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D11,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D12,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D13,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D14,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D15,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D16,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D17,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D18,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D19,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D20,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D21,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D22,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_D23,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_CS1_N,       DISPLAYA, PULL_UP,   NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_M1,          DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(LCD_DC1,         DISPLAYA, PULL_DOWN, NORMAL,      OUTPUT),
	DEFAULT_PINMUX(HDMI_INT,        RSVD0,  NORMAL,    TRISTATE,         INPUT),
	DEFAULT_PINMUX(DDC_SCL,         I2C4,  PULL_UP,   NORMAL,          INPUT),
	DEFAULT_PINMUX(DDC_SDA,         I2C4,  PULL_UP,   NORMAL,          INPUT),
	DEFAULT_PINMUX(CRT_HSYNC,       CRT,  PULL_UP,   TRISTATE,           INPUT),
	DEFAULT_PINMUX(CRT_VSYNC,       CRT,  PULL_UP,   TRISTATE,           INPUT),
	DEFAULT_PINMUX(UART2_RXD,       IRDA,  PULL_UP,   TRISTATE,          INPUT),
	DEFAULT_PINMUX(UART2_TXD,       IRDA,  PULL_UP,   TRISTATE,          INPUT),
	DEFAULT_PINMUX(UART2_RTS_N,     GMI, PULL_UP,   TRISTATE,           OUTPUT),
	DEFAULT_PINMUX(UART2_CTS_N,     GMI, PULL_UP,   TRISTATE,           OUTPUT),
	DEFAULT_PINMUX(UART3_TXD,       UARTC, NORMAL,    NORMAL,         OUTPUT),
	DEFAULT_PINMUX(UART3_RXD,       UARTC,  NORMAL,    NORMAL,         INPUT),
	DEFAULT_PINMUX(UART3_CTS_N,     UARTC,  NORMAL,    NORMAL,         INPUT),
	DEFAULT_PINMUX(UART3_RTS_N,     UARTC, NORMAL,    NORMAL,         OUTPUT),
	DEFAULT_PINMUX(GPIO_PU0,        OWR,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(GPIO_PU1,        RSVD1,  NORMAL,    TRISTATE,         INPUT),
	DEFAULT_PINMUX(GPIO_PU2,        RSVD1,  NORMAL,    TRISTATE,         INPUT),
	DEFAULT_PINMUX(GPIO_PU3,        PWM0,  NORMAL,    TRISTATE,          INPUT),
	DEFAULT_PINMUX(GPIO_PU4,        PWM1,  NORMAL,    TRISTATE,          INPUT),
	DEFAULT_PINMUX(GPIO_PU5,        PWM2,  PULL_DOWN, NORMAL,          INPUT),
	DEFAULT_PINMUX(GPIO_PU6,        PWM3,  NORMAL,    TRISTATE,          INPUT),
	DEFAULT_PINMUX(GEN1_I2C_SDA,    I2C1,  NORMAL,    NORMAL,          INPUT),
	DEFAULT_PINMUX(GEN1_I2C_SCL,    I2C1,  NORMAL,    NORMAL,          INPUT),
	DEFAULT_PINMUX(DAP4_FS,         I2S3,  PULL_DOWN, TRISTATE,          INPUT),
	DEFAULT_PINMUX(DAP4_DIN,        I2S3,  PULL_DOWN, TRISTATE,          INPUT),
	DEFAULT_PINMUX(DAP4_DOUT,       I2S3,  PULL_DOWN, TRISTATE,          INPUT),
	DEFAULT_PINMUX(DAP4_SCLK,       I2S3,  PULL_DOWN, TRISTATE,          INPUT),
	DEFAULT_PINMUX(CLK3_OUT,        EXTPERIPH3,  NORMAL,    NORMAL,    INPUT),
	DEFAULT_PINMUX(CLK3_REQ,        DEV3,  NORMAL,    NORMAL,          INPUT),
	DEFAULT_PINMUX(GMI_WP_N,        GMI,  PULL_UP,   TRISTATE,           INPUT),
	DEFAULT_PINMUX(GMI_IORDY,       RSVD1,  NORMAL,    TRISTATE,         INPUT),
	DEFAULT_PINMUX(GMI_WAIT,        GMI,  PULL_UP,   NORMAL,           INPUT),
	DEFAULT_PINMUX(GMI_ADV_N,       GMI,  NORMAL,    NORMAL,           INPUT),
	DEFAULT_PINMUX(GMI_CLK,         GMI,  NORMAL,    NORMAL,           INPUT),
	DEFAULT_PINMUX(GMI_CS0_N,       RSVD1,  NORMAL,    TRISTATE,         INPUT),
	DEFAULT_PINMUX(GMI_CS1_N,       GMI,  PULL_UP,   TRISTATE,           INPUT),
	DEFAULT_PINMUX(GMI_CS2_N,       GMI,  PULL_UP,   NORMAL,           INPUT),
	DEFAULT_PINMUX(GMI_CS3_N,       GMI,  PULL_UP,   NORMAL,           INPUT),
	DEFAULT_PINMUX(GMI_CS4_N,       RSVD1,  PULL_UP,   TRISTATE,         INPUT),
	DEFAULT_PINMUX(GMI_CS6_N,       GMI,  PULL_UP,   NORMAL,           INPUT),
	DEFAULT_PINMUX(GMI_CS7_N,       GMI,  PULL_UP,   TRISTATE,           INPUT),
	DEFAULT_PINMUX(GMI_AD0,         GMI,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(GMI_AD1,         GMI,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(GMI_AD2,         GMI,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(GMI_AD3,         GMI,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(GMI_AD4,         GMI,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(GMI_AD5,         GMI,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(GMI_AD6,         GMI,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(GMI_AD7,         GMI,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(GMI_AD8,         PWM0, NORMAL,    NORMAL,          OUTPUT),
	DEFAULT_PINMUX(GMI_AD9,         PWM1, NORMAL,    NORMAL,          OUTPUT),
	DEFAULT_PINMUX(GMI_AD10,        GMI,  PULL_DOWN, TRISTATE,           INPUT),
	DEFAULT_PINMUX(GMI_AD11,        GMI,  PULL_DOWN, TRISTATE,           INPUT),
	DEFAULT_PINMUX(GMI_AD12,        RSVD1,  NORMAL,    TRISTATE,         INPUT),
	DEFAULT_PINMUX(GMI_AD13,        RSVD1,  NORMAL,    TRISTATE,         INPUT),
	DEFAULT_PINMUX(GMI_AD14,        RSVD1, NORMAL,    NORMAL,         OUTPUT),
	DEFAULT_PINMUX(GMI_AD15,        GMI,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(GMI_A16,         SPI4,  NORMAL,    NORMAL,          INPUT),
	DEFAULT_PINMUX(GMI_A17,         SPI4,  NORMAL,    NORMAL,          INPUT),
	DEFAULT_PINMUX(GMI_A18,         SPI4,  NORMAL,    NORMAL,          INPUT),
	DEFAULT_PINMUX(GMI_A19,         UARTD, NORMAL,    NORMAL,         OUTPUT),
	DEFAULT_PINMUX(GMI_WR_N,        GMI,  NORMAL,    NORMAL,           INPUT),
	DEFAULT_PINMUX(GMI_OE_N,        GMI,  NORMAL,    NORMAL,           INPUT),
	DEFAULT_PINMUX(GMI_DQS,         RSVD1,  NORMAL,    NORMAL,         INPUT),
	DEFAULT_PINMUX(GMI_RST_N,       GMI,  PULL_UP,   NORMAL,           INPUT),
	DEFAULT_PINMUX(GEN2_I2C_SCL,    I2C2,  PULL_UP,   NORMAL,          INPUT),
	DEFAULT_PINMUX(GEN2_I2C_SDA,    I2C2,  PULL_UP,   NORMAL,          INPUT),
	DEFAULT_PINMUX(SDMMC4_CLK,      SDIO4,  NORMAL,    NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC4_CMD,      SDIO4,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT0,     SDIO4,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT1,     SDIO4,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT2,     SDIO4,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT3,     SDIO4,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT4,     SDIO4,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT5,     SDIO4,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT6,     SDIO4,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC4_DAT7,     SDIO4,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC4_RST_N,    POPSDMMC4,  PULL_UP,   NORMAL,     INPUT),
	DEFAULT_PINMUX(CAM_MCLK,        VI_ALT2, NORMAL,    NORMAL,       OUTPUT),
	DEFAULT_PINMUX(GPIO_PCC1,       I2S4,  PULL_UP,   TRISTATE,          INPUT),
	DEFAULT_PINMUX(GPIO_PBB0,       I2S4, NORMAL,    NORMAL,          OUTPUT),
	DEFAULT_PINMUX(CAM_I2C_SCL,     I2C3,  PULL_UP,   NORMAL,          INPUT),
	DEFAULT_PINMUX(CAM_I2C_SDA,     I2C3,  PULL_UP,   NORMAL,          INPUT),
	DEFAULT_PINMUX(GPIO_PBB3,       VGP3,  NORMAL,    TRISTATE,          INPUT),
	DEFAULT_PINMUX(GPIO_PBB4,       VGP4, NORMAL,    NORMAL,          OUTPUT),
	DEFAULT_PINMUX(GPIO_PBB5,       VGP5,  NORMAL,    TRISTATE,          INPUT),
	DEFAULT_PINMUX(GPIO_PBB6,       VGP6,  NORMAL,    NORMAL,          INPUT),
	DEFAULT_PINMUX(GPIO_PBB7,       I2S4, NORMAL,    NORMAL,          OUTPUT),
	DEFAULT_PINMUX(GPIO_PCC2,       I2S4,  PULL_UP,   NORMAL,          INPUT),
	DEFAULT_PINMUX(JTAG_RTCK,       RTCK,  PULL_UP,   NORMAL,          INPUT),
	DEFAULT_PINMUX(PWR_I2C_SCL,     I2CPWR,  PULL_UP,   NORMAL,        INPUT),
	DEFAULT_PINMUX(PWR_I2C_SDA,     I2CPWR,  PULL_UP,   NORMAL,        INPUT),
	DEFAULT_PINMUX(KB_ROW0,         KBC,  PULL_DOWN, NORMAL,           INPUT),
	DEFAULT_PINMUX(KB_ROW1,         KBC,  PULL_DOWN, NORMAL,           INPUT),
	DEFAULT_PINMUX(KB_ROW2,         KBC, NORMAL,    NORMAL,           OUTPUT),
	DEFAULT_PINMUX(KB_ROW3,         KBC, NORMAL,    NORMAL,           OUTPUT),
	DEFAULT_PINMUX(KB_ROW4,         KBC, NORMAL,    TRISTATE,           OUTPUT),
	DEFAULT_PINMUX(KB_ROW5,         KBC,  PULL_DOWN, NORMAL,           INPUT),
	DEFAULT_PINMUX(KB_ROW6,         KBC, NORMAL,    NORMAL,           OUTPUT),
	DEFAULT_PINMUX(KB_ROW7,         KBC, NORMAL,    NORMAL,           OUTPUT),
	DEFAULT_PINMUX(KB_ROW8,         KBC,  NORMAL,    NORMAL,           INPUT),
	DEFAULT_PINMUX(KB_ROW9,         KBC, NORMAL,    TRISTATE,           OUTPUT),
	DEFAULT_PINMUX(KB_ROW10,        KBC,  NORMAL,    NORMAL,           INPUT),
	DEFAULT_PINMUX(KB_ROW11,        KBC, NORMAL,    NORMAL,           OUTPUT),
	DEFAULT_PINMUX(KB_ROW12,        KBC,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(KB_ROW13,        KBC,  PULL_UP,   NORMAL,           INPUT),
	DEFAULT_PINMUX(KB_ROW14,        KBC,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(KB_ROW15,        KBC,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(KB_COL0,         KBC,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(KB_COL1,         KBC,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(KB_COL2,         KBC,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(KB_COL3,         KBC,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(KB_COL4,         KBC,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(KB_COL5,         KBC,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(KB_COL6,         KBC,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(KB_COL7,         KBC,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(CLK_32K_OUT,     BLINK, NORMAL,    NORMAL,         OUTPUT),
	DEFAULT_PINMUX(SYS_CLK_REQ,     SYSCLK,  NORMAL,    NORMAL,        INPUT),
	DEFAULT_PINMUX(CORE_PWR_REQ,    RSVD,  NORMAL,    NORMAL,         INPUT),
	DEFAULT_PINMUX(CPU_PWR_REQ,     RSVD,  NORMAL,    NORMAL,         INPUT),
	DEFAULT_PINMUX(PWR_INT_N,       RSVD,  NORMAL,    NORMAL,         INPUT),
	DEFAULT_PINMUX(CLK_32K_IN,      RSVD,  NORMAL,    NORMAL,         INPUT),
	DEFAULT_PINMUX(OWR,             OWR,  PULL_UP,   NORMAL,           INPUT),
	DEFAULT_PINMUX(DAP1_FS,         I2S0, NORMAL,    NORMAL,          OUTPUT),
	DEFAULT_PINMUX(DAP1_DIN,        I2S0,  PULL_DOWN, TRISTATE,          INPUT),
	DEFAULT_PINMUX(DAP1_DOUT,       I2S0,  PULL_DOWN, TRISTATE,          INPUT),
	DEFAULT_PINMUX(DAP1_SCLK,       I2S0, NORMAL,    NORMAL,          OUTPUT),
	DEFAULT_PINMUX(CLK1_REQ,        DAP,  NORMAL,    TRISTATE,           INPUT),
	DEFAULT_PINMUX(CLK1_OUT,        EXTPERIPH1,  NORMAL,    NORMAL,    INPUT),
	DEFAULT_PINMUX(SPDIF_IN,        SPDIF,  PULL_UP,   TRISTATE,         INPUT),
	DEFAULT_PINMUX(SPDIF_OUT,       SPDIF,  PULL_UP,   TRISTATE,         INPUT),
	DEFAULT_PINMUX(DAP2_FS,         I2S1,  NORMAL,    NORMAL,          INPUT),
	DEFAULT_PINMUX(DAP2_DIN,        I2S1,  NORMAL,    NORMAL,          INPUT),
	DEFAULT_PINMUX(DAP2_DOUT,       I2S1,  NORMAL,    NORMAL,          INPUT),
	DEFAULT_PINMUX(DAP2_SCLK,       I2S1,  NORMAL,    NORMAL,          INPUT),
	DEFAULT_PINMUX(SPI2_MOSI,       SPI6,  PULL_DOWN, TRISTATE,          INPUT),
	DEFAULT_PINMUX(SPI2_MISO,       SPI6,  PULL_DOWN, TRISTATE,          INPUT),
	DEFAULT_PINMUX(SPI2_CS0_N,      SPI6,  PULL_UP,   NORMAL,          INPUT),
	DEFAULT_PINMUX(SPI2_SCK,        SPI6,  PULL_UP,   NORMAL,          INPUT),
	DEFAULT_PINMUX(SPI1_MOSI,       SPI2,  PULL_UP,   TRISTATE,          INPUT),
	DEFAULT_PINMUX(SPI1_SCK,        SPI2,  PULL_UP,   TRISTATE,          INPUT),
	DEFAULT_PINMUX(SPI1_CS0_N,      SPI2,  PULL_UP,   TRISTATE,          INPUT),
	DEFAULT_PINMUX(SPI1_MISO,       SPI1,  PULL_DOWN, TRISTATE,          INPUT),
	DEFAULT_PINMUX(SDMMC3_CLK,      SDIO3,  NORMAL,    NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC3_CMD,      SDIO3,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC3_DAT0,     SDIO3,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC3_DAT1,     SDIO3,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC3_DAT2,     SDIO3,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC3_DAT3,     SDIO3,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC3_DAT4,     PWM1, NORMAL,    NORMAL,          OUTPUT),
	DEFAULT_PINMUX(SDMMC3_DAT5,     SDIO3,  PULL_UP,   NORMAL,         INPUT),
	DEFAULT_PINMUX(SDMMC3_DAT6,     SPDIF,  NORMAL,    TRISTATE,         INPUT),
	DEFAULT_PINMUX(SDMMC3_DAT7,     SPDIF,  NORMAL,    TRISTATE,         INPUT),
	DEFAULT_PINMUX(PEX_L0_PRSNT_N,  PCIE, NORMAL,    NORMAL,          OUTPUT),
	DEFAULT_PINMUX(PEX_L0_RST_N,    PCIE, NORMAL,    NORMAL,          OUTPUT),
	DEFAULT_PINMUX(PEX_L0_CLKREQ_N, PCIE, NORMAL,    NORMAL,          OUTPUT),
	DEFAULT_PINMUX(PEX_WAKE_N,      PCIE,  NORMAL,    TRISTATE,          INPUT),
	DEFAULT_PINMUX(PEX_L1_PRSNT_N,  PCIE, NORMAL,    NORMAL,          OUTPUT),
	DEFAULT_PINMUX(PEX_L1_RST_N,    PCIE,  NORMAL,    TRISTATE,          INPUT),
	DEFAULT_PINMUX(PEX_L1_CLKREQ_N, PCIE, NORMAL,    NORMAL,          OUTPUT),
	DEFAULT_PINMUX(PEX_L2_PRSNT_N,  PCIE, NORMAL,    NORMAL,          OUTPUT),
	DEFAULT_PINMUX(PEX_L2_RST_N,    PCIE,  NORMAL,    TRISTATE,          INPUT),
	DEFAULT_PINMUX(PEX_L2_CLKREQ_N, PCIE, NORMAL,    NORMAL,          OUTPUT),
	DEFAULT_PINMUX(HDMI_CEC,        CEC,  NORMAL,    NORMAL,           INPUT),
        DEFAULT_PINMUX(VI_D0,           RSVD1,  PULL_DOWN, TRISTATE,       INPUT),
        DEFAULT_PINMUX(VI_D1,           RSVD1,  PULL_DOWN, TRISTATE,       INPUT),
        DEFAULT_PINMUX(VI_D2,           RSVD1,  PULL_DOWN, TRISTATE,       INPUT),
        DEFAULT_PINMUX(VI_D3,           RSVD1,  NORMAL,    TRISTATE,       INPUT),
        DEFAULT_PINMUX(VI_D4,           RSVD1,  PULL_DOWN, TRISTATE,       INPUT),
        DEFAULT_PINMUX(VI_D5,           RSVD1,  PULL_DOWN, TRISTATE,       INPUT),
        DEFAULT_PINMUX(VI_D6,           RSVD1,  PULL_DOWN, TRISTATE,       INPUT),
        DEFAULT_PINMUX(VI_D7,           RSVD1, NORMAL,    NORMAL,       OUTPUT),
        DEFAULT_PINMUX(VI_D8,           RSVD1, NORMAL,    NORMAL,       OUTPUT),
        DEFAULT_PINMUX(VI_D9,           RSVD1, NORMAL,    NORMAL,       OUTPUT),
        DEFAULT_PINMUX(VI_D10,          RSVD1,  PULL_DOWN, TRISTATE,       INPUT),
        DEFAULT_PINMUX(VI_D11,          RSVD1,  NORMAL,    NORMAL,       INPUT),
        DEFAULT_PINMUX(VI_PCLK,         RSVD1,  PULL_DOWN, TRISTATE,         INPUT),
//      DEFAULT_PINMUX(VI_MCLK,         UNKNOWN,  PULL_DOWN, TRISTATE,       INPUT),
        DEFAULT_PINMUX(VI_VSYNC,        RSVD1,  PULL_DOWN, TRISTATE,       INPUT),
        DEFAULT_PINMUX(VI_HSYNC,        RSVD1, NORMAL,    NORMAL,       OUTPUT),
//      DEFAULT_PINMUX(SPI2_CS1_N,      UNKNOWN,  PULL_UP,   NORMAL,       INPUT),
//      DEFAULT_PINMUX(SPI2_CS2_N,      UNKNOWN,  NORMAL,    NORMAL,       INPUT),
	
};

/*Do not use for now*/
static __initdata struct tegra_pingroup_config unused_pins_lowpower[] = {


};

/* We are disabling this code for now. */
#define GPIO_INIT_PIN_MODE(_gpio, _is_input, _value)	\
	{					\
		.gpio_nr	= _gpio,	\
		.is_input	= _is_input,	\
		.value		= _value,	\
	}

static struct gpio_init_pin_info init_gpio_mode_surface_rt_common[] = {
	//GPIO_INIT_PIN_MODE(TEGRA_GPIO_PDD7, false, 0),
	//GPIO_INIT_PIN_MODE(TEGRA_GPIO_PCC6, false, 0),
	//GPIO_INIT_PIN_MODE(TEGRA_GPIO_PR0, false, 0),
	//GPIO_INIT_PIN_MODE(TEGRA_GPIO_PU4, false, 1),
};

static void __init surface_rt_gpio_init_configure(void)
{
	int len;
	int i;
	struct gpio_init_pin_info *pins_info;

	len = ARRAY_SIZE(init_gpio_mode_surface_rt_common);
	pins_info = init_gpio_mode_surface_rt_common;
	
	for (i = 0; i < len; ++i) {
		tegra_gpio_init_configure(pins_info->gpio_nr,
			pins_info->is_input, pins_info->value);
		pins_info++;
	}
}

int __init surface_rt_pinmux_init(void)
{
	surface_rt_gpio_init_configure();

	tegra_pinmux_config_table(surface_rt_pinmux_common, ARRAY_SIZE(surface_rt_pinmux_common));
	tegra_drive_pinmux_config_table(surface_rt_drive_pinmux,
					ARRAY_SIZE(surface_rt_drive_pinmux));

	tegra_pinmux_config_table(unused_pins_lowpower,
		ARRAY_SIZE(unused_pins_lowpower));

	return 0;
}

void __init surface_rt_pinmux_init_early(void)
{
	tegra30_default_pinmux();
}

#define PIN_GPIO_LPM(_name, _gpio, _is_input, _value)	\
	{					\
		.name		= _name,	\
		.gpio_nr	= _gpio,	\
		.is_gpio	= true,		\
		.is_input	= _is_input,	\
		.value		= _value,	\
	}

struct gpio_init_pin_info pin_lpm_surface_rt_common[] = {
	//PIN_GPIO_LPM("GMI_CS7",   TEGRA_GPIO_PI6, 1, 0),
};

static void set_unused_pin_gpio(struct gpio_init_pin_info *lpm_pin_info,
		int list_count)
{
	int i;
	struct gpio_init_pin_info *pin_info;
	int ret;

	for (i = 0; i < list_count; ++i) {
		pin_info = (struct gpio_init_pin_info *)(lpm_pin_info + i);
		if (!pin_info->is_gpio)
			continue;

		ret = gpio_request(pin_info->gpio_nr, pin_info->name);
		if (ret < 0) {
			pr_err("%s: error in gpio_request() for gpio %d\n",
					__func__, pin_info->gpio_nr);
			continue;
		}
		if (pin_info->is_input)
			ret = gpio_direction_input(pin_info->gpio_nr);
		else
			ret = gpio_direction_output(pin_info->gpio_nr,
							pin_info->value);
		if (ret < 0) {
			pr_err("%s: error in setting gpio %d to in/out\n",
				__func__, pin_info->gpio_nr);
			gpio_free(pin_info->gpio_nr);
			continue;
		}
	}
}

/* Initialize the pins to desired state as per power/asic/system-eng
 * recomendation */
int __init surface_rt_pins_state_init(void)
{
	set_unused_pin_gpio(&pin_lpm_surface_rt_common[0],
			    ARRAY_SIZE(pin_lpm_surface_rt_common));

	return 0;
}
