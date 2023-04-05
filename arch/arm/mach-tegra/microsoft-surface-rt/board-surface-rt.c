/*
 * arch/arm/mach-tegra/board-surface-rt.c
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

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/serial_8250.h>
#include <linux/i2c.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/i2c-tegra.h>
#include <linux/gpio.h>
#include <linux/input.h>
#include <linux/platform_data/tegra_usb.h>
#include <linux/spi/spi.h>
#include <linux/tegra_uart.h>
#include <linux/memblock.h>
#include <linux/spi-tegra.h>
#include <linux/nfc/pn544.h>
#include <linux/skbuff.h>
#include <linux/regulator/consumer.h>
#include <linux/power/smb347-charger.h>
#include <linux/power/gpio-charger.h>
#include <linux/rfkill-gpio.h>

#include <asm/hardware/gic.h>

#include <mach/clk.h>
#include <mach/iomap.h>
#include <mach/irqs.h>
#include <mach/pinmux.h>
#include <mach/pinmux-tegra30.h>
#include <mach/iomap.h>
#include <mach/io.h>
#include <mach/i2s.h>
#include <mach/tegra_asoc_pdata.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/usb_phy.h>
#include <mach/tegra_fiq_debugger.h>


#include <sound/wm8962.h>
#include <mach/tegra_wm8962_pdata.h>

#include "../board.h"
#include "../board-common.h"
#include "../clock.h"
#include "board-surface-rt.h"
#include "../devices.h"
#include "../gpio-names.h"
#include "../fuse.h"
#include "../pm.h"
#include "../wdt-recovery.h"
#include "../common.h"


#define TEGRA_BATTERY_EN TEGRA_GPIO_PN3


static __initdata struct tegra_clk_init_table surface_rt_clk_init_table[] = {
	/* name		parent		rate		enabled */
	{"pll_m",	NULL,		0,		false},
	{"audio1",	"i2s1_sync",	0,		false},
	{"audio2",	"i2s2_sync",	0,		false},
	{"audio3",	"i2s3_sync",	0,		false},
	{"audio4",	"i2s4_sync",	0,		false},
	{"blink",	"clk_32k",	32768,		true},
	{"d_audio",	"clk_m",	12000000,	false},
	{"dam0",	"clk_m",	12000000,	false},
	{"dam1",	"clk_m",	12000000,	false},
	{"dam2",	"clk_m",	12000000,	false},
	{"hda",		"pll_p",	108000000,	false},
	{"hda2codec_2x","pll_p",	48000000,	false},
	{"i2c1",	"pll_p",	3200000,	false},
	{"i2c2",	"pll_p",	3200000,	false},
	{"i2c3",	"pll_p",	3200000,	false},
	{"i2c4",	"pll_p",	3200000,	false},
	{"i2c5",	"pll_p",	3200000,	false},
	{"i2s0",	"pll_a_out0",	0,		false},
	{"i2s1",	"pll_a_out0",	0,		false},
	{"i2s2",	"pll_a_out0",	0,		false},
	{"i2s3",	"pll_a_out0",	0,		false},
	{"i2s4",	"pll_a_out0",	0,		false},
	{"pll_p",	NULL,		0,		false},
	{"pll_c",       NULL,           0,              false},
	{"pwm",		"pll_p",	5100000,	false},
	{"spdif_out",	"pll_a_out0",	0,		false},
	{NULL,		NULL,		0,		0},
};

static struct tegra_i2c_platform_data surface_rt_i2c1_platform_data = { //MS HID
	.adapter_nr	= 0,
	.bus_count	= 1,
	.bus_clk_rate	= { 100000, 0 },
	.scl_gpio		= {TEGRA_GPIO_PC4, 0},
	.sda_gpio		= {TEGRA_GPIO_PC5, 0},
	.arb_recovery = arb_lost_recovery,
};

static struct tegra_i2c_platform_data surface_rt_i2c2_platform_data = { //touch hid
	.adapter_nr	= 1,
	.bus_count	= 1,
	.bus_clk_rate	= { 100000, 0 },
	.is_clkon_always = true,
	.scl_gpio		= {TEGRA_GPIO_PT5, 0},
	.sda_gpio		= {TEGRA_GPIO_PT6, 0},
	.arb_recovery = arb_lost_recovery,
};
/*
static struct tegra_i2c_platform_data surface_rt_i2c3_platform_data = { //cameras
	.adapter_nr	= 2,
	.bus_count	= 1,
	.bus_clk_rate	= { 100000, 0 },
	.scl_gpio		= {TEGRA_GPIO_PBB1, 0},
	.sda_gpio		= {TEGRA_GPIO_PBB2, 0},
	.arb_recovery = arb_lost_recovery,
};

static struct tegra_i2c_platform_data surface_rt_i2c4_platform_data = { //HDMI display data channel ?? 
	.adapter_nr	= 3,
	.bus_count	= 1,
	.bus_clk_rate	= { 100000, 0 },
	.scl_gpio		= {TEGRA_GPIO_PV4, 0},
	.sda_gpio		= {TEGRA_GPIO_PV5, 0},
	.arb_recovery = arb_lost_recovery,
};

*/
static struct tegra_i2c_platform_data surface_rt_i2c5_platform_data = { // system bus - embedded controller / wm8962 audio
	.adapter_nr	= 4,
	.bus_count	= 1,
	.bus_clk_rate	= { 400000, 0 },
	.scl_gpio		= {TEGRA_GPIO_PZ6, 0},
	.sda_gpio		= {TEGRA_GPIO_PZ7, 0},
	.arb_recovery = arb_lost_recovery,
};

#ifdef CONFIG_SND_SOC_TEGRA_WM8962

static struct wm8962_pdata surface_rt_wm8962_pdata = {
	.irq_active_low = 0,
	.mic_cfg = 0,
	.gpio_base = SURFACE_RT_GPIO_WM8962(0),
	.gpio_init = {
		/* Needs to be filled for digital Mic's */
	},
};

static struct i2c_board_info __initdata surface_rt_codec_wm8962_info = {
	I2C_BOARD_INFO("wm8962", 0x1a),
	.platform_data = &surface_rt_wm8962_pdata,
};

#endif

static void surface_rt_i2c_init(void)
{
	tegra_i2c_device1.dev.platform_data = &surface_rt_i2c1_platform_data;
	tegra_i2c_device2.dev.platform_data = &surface_rt_i2c2_platform_data;
//	tegra_i2c_device3.dev.platform_data = &surface_rt_i2c3_platform_data;
//	tegra_i2c_device4.dev.platform_data = &surface_rt_i2c4_platform_data;
	tegra_i2c_device5.dev.platform_data = &surface_rt_i2c5_platform_data;

	platform_device_register(&tegra_i2c_device5);
//	platform_device_register(&tegra_i2c_device4);
//	platform_device_register(&tegra_i2c_device3);
	platform_device_register(&tegra_i2c_device2);
	platform_device_register(&tegra_i2c_device1);

}

static struct platform_device *surface_rt_uart_devices[] __initdata = {
	&tegra_uartb_device,
	&tegra_uartc_device,
	&tegra_uartd_device,
	&tegra_uarte_device,
};
static struct uart_clk_parent uart_parent_clk[] = {
	[0] = {.name = "clk_m"},
	[1] = {.name = "pll_p"},
#ifndef CONFIG_TEGRA_PLLM_RESTRICTED
	[2] = {.name = "pll_m"},
#endif
};

static struct tegra_uart_platform_data surface_rt_uart_pdata;
static struct tegra_uart_platform_data surface_rt_loopback_uart_pdata;

static unsigned int debug_uart_port_irq;

static struct platform_device *debug_uarts[] = {
	&debug_uarta_device,
	&debug_uartb_device,
	&debug_uartc_device,
	&debug_uartd_device,
	&debug_uarte_device,
};

static void __init uart_debug_init(void)
{
	int debug_port_id;
	return;
	debug_port_id = uart_console_debug_init(2);
	if (debug_port_id < 0)
		return;

	if (debug_port_id >= ARRAY_SIZE(debug_uarts)) {
		pr_info("The debug console id %d is invalid, Assuming UARTA",
			debug_port_id);
		debug_port_id = 0;
	}

	surface_rt_uart_devices[debug_port_id] = uart_console_debug_device;
}

static void __init surface_rt_uart_init(void)
{
	struct clk *c;
	int i;

	for (i = 0; i < ARRAY_SIZE(uart_parent_clk); ++i) {
		c = tegra_get_clock_by_name(uart_parent_clk[i].name);
		if (IS_ERR_OR_NULL(c)) {
			pr_err("Not able to get the clock for %s\n",
						uart_parent_clk[i].name);
			continue;
		}
		uart_parent_clk[i].parent_clk = c;
		uart_parent_clk[i].fixed_clk_rate = clk_get_rate(c);
	}
	surface_rt_uart_pdata.parent_clk_list = uart_parent_clk;
	surface_rt_uart_pdata.parent_clk_count = ARRAY_SIZE(uart_parent_clk);
	surface_rt_loopback_uart_pdata.parent_clk_list = uart_parent_clk;
	surface_rt_loopback_uart_pdata.parent_clk_count =
						ARRAY_SIZE(uart_parent_clk);
	surface_rt_loopback_uart_pdata.is_loopback = true;
	tegra_uartb_device.dev.platform_data = &surface_rt_uart_pdata;
	tegra_uartc_device.dev.platform_data = &surface_rt_uart_pdata;
	tegra_uartd_device.dev.platform_data = &surface_rt_uart_pdata;
	/* UARTE is used for loopback test purpose */
	tegra_uarte_device.dev.platform_data = &surface_rt_loopback_uart_pdata;

	/* Register low speed only if it is selected */
	if (!is_tegra_debug_uartport_hs())
		uart_debug_init();

	tegra_serial_debug_init(debug_uart_port_base, debug_uart_port_irq,
				debug_uart_clk, -1, -1);

	platform_add_devices(surface_rt_uart_devices,
				ARRAY_SIZE(surface_rt_uart_devices));
}


static struct platform_device *surface_rt_spi_devices[] __initdata = {
	&tegra_spi_device4,
};

static struct spi_clk_parent spi_parent_clk[] = {
	[0] = {.name = "pll_p"},
#ifndef CONFIG_TEGRA_PLLM_RESTRICTED
	[1] = {.name = "pll_m"},
	[2] = {.name = "clk_m"},
#else
	[1] = {.name = "clk_m"},
#endif
};

static struct tegra_spi_platform_data surface_rt_spi_pdata = {
	.is_dma_based		= true,
	.max_dma_buffer		= (16 * 1024),
	.is_clkon_always	= false,
	.max_rate		= 100000000,
};

static void __init surface_rt_spi_init(void)
{
	int i;
	struct clk *c;

	for (i = 0; i < ARRAY_SIZE(spi_parent_clk); ++i) {
		c = tegra_get_clock_by_name(spi_parent_clk[i].name);
		if (IS_ERR_OR_NULL(c)) {
			pr_err("Not able to get the clock for %s\n",
						spi_parent_clk[i].name);
			continue;
		}
		spi_parent_clk[i].parent_clk = c;
		spi_parent_clk[i].fixed_clk_rate = clk_get_rate(c);
	}

	surface_rt_spi_pdata.parent_clk_list = spi_parent_clk;
	surface_rt_spi_pdata.parent_clk_count = ARRAY_SIZE(spi_parent_clk);
	tegra_spi_device4.dev.platform_data = &surface_rt_spi_pdata;
	platform_add_devices(surface_rt_spi_devices,
				ARRAY_SIZE(surface_rt_spi_devices));
}

static struct resource tegra_rtc_resources[] = {
	[0] = {
		.start = TEGRA_RTC_BASE,
		.end = TEGRA_RTC_BASE + TEGRA_RTC_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = INT_RTC,
		.end = INT_RTC,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device tegra_rtc_device = {
	.name	= "tegra_rtc",
	.id		= -1,
	.resource = tegra_rtc_resources,
	.num_resources = ARRAY_SIZE(tegra_rtc_resources),
};

#ifdef CONFIG_SND_SOC_TEGRA_WM8962

static void surface_rt_sound_init(void)

{
        surface_rt_codec_wm8962_info.irq = gpio_to_irq(TEGRA_GPIO_CDC_IRQ);
        i2c_register_board_info(4, &surface_rt_codec_wm8962_info, 1);

        printk(KERN_INFO "Surface RT Sound Init \n");
}

static struct tegra_asoc_platform_data cardhu_audio_wm8962_pdata = {
        .gpio_spkr_en           = TEGRA_GPIO_SPKR_EN,
        .gpio_hp_det            = TEGRA_GPIO_HP_DET,
        .gpio_hp_mute           = -1,
        .gpio_int_mic_en        = -1,
        .gpio_ext_mic_en        = -1,
//      .gpio_bypass_switch_en  = TEGRA_GPIO_BYPASS_SWITCH_EN,
//      .gpio_debug_switch_en   = TEGRA_GPIO_DEBUG_SWITCH_EN,
        .i2s_param[HIFI_CODEC]  = {
                .audio_port_id  = 0,
                .is_i2s_master  = 1,
                .i2s_mode       = TEGRA_DAIFMT_I2S,
        },
//      .i2s_param[BASEBAND]    = {
//              .audio_port_id  = -1,
//      },
//      .i2s_param[BT_SCO]      = {
//              .audio_port_id  = 3,
//              .is_i2s_master  = 1,
//              .i2s_mode       = TEGRA_DAIFMT_DSP_A,
//      },
};

static struct platform_device cardhu_audio_wm8962_device = {
        .name   = "tegra-snd-wm8962",
        .id     = 0,
        .dev    = {
                .platform_data = &cardhu_audio_wm8962_pdata,
        },
};

#endif

static struct platform_device *surface_rt_devices[] __initdata = {
	&tegra_pmu_device,
	&tegra_rtc_device,
	&tegra_udc_device,
	&tegra_wdt0_device,
#ifdef CONFIG_TEGRA_AVP
	&tegra_avp_device,
#endif
#ifdef CONFIG_CRYPTO_DEV_TEGRA_SE
	&tegra_se_device,
#endif
	&tegra_ahub_device,
	&tegra_dam_device0,
	&tegra_dam_device1,
	&tegra_dam_device2,
	&tegra_i2s_device1,
	&tegra_i2s_device2,
	&tegra_i2s_device3,
	&tegra_i2s_device4,
	&tegra_spdif_device,
	&spdif_dit_device,
	&bluetooth_dit_device,
	&tegra_pcm_device,
	&tegra_hda_device,
#ifdef CONFIG_SND_SOC_TEGRA_WM8962

	&cardhu_audio_wm8962_device,
#endif

#ifdef CONFIG_CRYPTO_DEV_TEGRA_AES
	&tegra_aes_device,
#endif
};

#if defined(CONFIG_USB_SUPPORT)
static void surface_rt_otg_power(int enable)
{
//	struct power_supply *smb_usb = power_supply_get_by_name("smb347-usb");
//	union power_supply_propval usb_otg = { enable };

	pr_debug("%s: %d\n", __func__, enable);

//	if (smb_usb && smb_usb->set_property)
//		smb_usb->set_property(
//			smb_usb,
//			POWER_SUPPLY_PROP_USB_OTG,
//			&usb_otg);
//	else
//		pr_err("%s: couldn't get power supply\n", __func__);
}

void surface_rt_usb_ehci1_phy_on(void)
{
	surface_rt_otg_power(1);
}

void surface_rt_usb_echi1_phy_off(void)
{
	surface_rt_otg_power(0);
}

static struct tegra_usb_phy_platform_ops ehci1_pdata_ops = {
	.pre_phy_on = &surface_rt_usb_ehci1_phy_on,
	.post_phy_off = &surface_rt_usb_echi1_phy_off,
};

static struct tegra_usb_platform_data tegra_udc_pdata = {
	.port_otg = true,
	.has_hostpc = true,
	.phy_intf = TEGRA_USB_PHY_INTF_UTMI,
	.op_mode = TEGRA_USB_OPMODE_DEVICE,
	.u_data.dev = {
		.vbus_pmu_irq = 0,
		.vbus_gpio = TEGRA_GPIO_PDD6,
		.charging_supported = false,
		.remote_wakeup_supported = false,
	},
	.u_cfg.utmi = {
		.hssync_start_delay = 0,
		.elastic_limit = 16,
		.idle_wait_delay = 17,
		.term_range_adj = 6,
		.xcvr_setup = 8,
		.xcvr_lsfslew = 2,
		.xcvr_lsrslew = 2,
		.xcvr_setup_offset = 0,
		.xcvr_use_fuses = 1,
	},
};

static struct tegra_usb_platform_data tegra_ehci1_utmi_pdata = {
	.port_otg = true,
	.has_hostpc = true,
	.phy_intf = TEGRA_USB_PHY_INTF_UTMI,
	.op_mode = TEGRA_USB_OPMODE_HOST,
	.u_data.host = {
		.vbus_gpio = TEGRA_GPIO_PDD6,
		.hot_plug = true,
		.remote_wakeup_supported = true,
		.power_off_on_suspend = true,
	},
	.u_cfg.utmi = {
		.hssync_start_delay = 0,
		.elastic_limit = 16,
		.idle_wait_delay = 17,
		.term_range_adj = 6,
		.xcvr_setup = 15,
		.xcvr_lsfslew = 2,
		.xcvr_lsrslew = 2,
		.xcvr_setup_offset = 0,
		.xcvr_use_fuses = 1,
	},
	.ops = &ehci1_pdata_ops,
};

static struct tegra_usb_otg_data tegra_otg_pdata = {
	.ehci_device = &tegra_ehci1_device,
	.ehci_pdata = &tegra_ehci1_utmi_pdata,
};

static void surface_rt_usb_init(void)
{
	pr_info("%s\n", __func__);
	/* OTG should be the first to be registered */
	tegra_otg_device.dev.platform_data = &tegra_otg_pdata;
	platform_device_register(&tegra_otg_device);

	/* Setup the udc platform data */
	tegra_udc_device.dev.platform_data = &tegra_udc_pdata;
}
#else
static void surface_rt_usb_init(void) { }
#endif

void surface_rt_booting_info(void)
{
	static void __iomem *pmc = IO_ADDRESS(TEGRA_PMC_BASE);
	unsigned int reg;
	#define PMC_RST_STATUS_WDT (1)
	#define PMC_RST_STATUS_SW (3)

	reg = readl(pmc + 0x1b4);
	pr_info("%s: reg=%x\n", __func__, reg);

	if (reg == PMC_RST_STATUS_SW) {
		pr_info("%s: SW reboot\n", __func__);
	} else if (reg == PMC_RST_STATUS_WDT) {
		pr_info("%s: watchdog reboot\n", __func__);
	} else {
		pr_info("%s: normal\n", __func__);
	}
}


static void surface_rt_ec_init(void)

{
	int ret;
	ret = gpio_request(TEGRA_BATTERY_EN, "ec_gpio");
	if (ret)
	{
		pr_err("EC gpio request failed:%d\n", ret);
		return ;
	}
	else
	{
		gpio_direction_output(TEGRA_BATTERY_EN, 1);
		mdelay(100);
 	        gpio_set_value(TEGRA_BATTERY_EN, 1);
		mdelay(100);
		ret = gpio_get_value(TEGRA_BATTERY_EN);

		printk(KERN_INFO "Set Surface EC GPIO : %d\n",ret);
	}
}

static void __init tegra_surface_rt_init(void)
{
	tegra_clk_init_from_table(surface_rt_clk_init_table);
	tegra_enable_pinmux();
	tegra_smmu_init();
	tegra_soc_device_init("surface-rt");
	surface_rt_pinmux_init_early();
	surface_rt_pinmux_init();
	surface_rt_booting_info();
	surface_rt_i2c_init();
	surface_rt_spi_init();
	surface_rt_usb_init();
#ifdef CONFIG_TEGRA_EDP_LIMITS
	surface_rt_edp_init();
#endif
	surface_rt_uart_init();
	platform_add_devices(surface_rt_devices, ARRAY_SIZE(surface_rt_devices));
	tegra_ram_console_debug_init();
	surface_rt_regulator_init();
	surface_rt_sdhci_init();
	surface_rt_ec_init();
	surface_rt_suspend_init();
	surface_rt_keys_init();
	surface_rt_panel_init();
	surface_rt_sensors_init();
	surface_rt_pins_state_init();
	surface_rt_emc_init(); // need to fix first
#ifdef CONFIG_SND_SOC_TEGRA_WM8962

	surface_rt_sound_init();
#endif
	tegra_release_bootloader_fb();
#ifdef CONFIG_TEGRA_WDT_RECOVERY
	tegra_wdt_recovery_init();
#endif
	tegra_register_fuse();
	surface_rt_i2c_hid_init();
}

static void __init tegra_surface_rt_reserve(void)
{
#ifdef CONFIG_NVMAP_CONVERT_CARVEOUT_TO_IOVMM
	/* support 1920X1200 with 24bpp */
	tegra_reserve(0, SZ_8M + SZ_1M, SZ_16M);
#else
	tegra_reserve(SZ_128M, SZ_8M + SZ_8M);
#endif
}

static const char *cardhu_dt_board_compat[] = {
	"nvidia,cardhu",
	NULL
};

MACHINE_START(SURFACE_RT, "surface-rt")
	.atag_offset	= 0x100,
	.soc 		= &tegra_soc_desc,
	.map_io	= tegra_map_common_io,
	.reserve	= tegra_surface_rt_reserve,
	.init_early	= tegra30_init_early,
	.init_irq	= tegra_init_irq,
	.handle_irq 	= gic_handle_irq,
	.timer		= &tegra_timer,
	.init_machine	= tegra_surface_rt_init,
.dt_compat		= cardhu_dt_board_compat,
	.restart	= tegra_assert_system_reset,
MACHINE_END
