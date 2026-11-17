// SPDX-License-Identifier: MIT
/*
 * justiceBL LED0 驱动 —— 核心板 LED0，GPIO1_IO03，低电平点亮。
 *
 * 依据：kernel dts imx6ull-alientek-emmc.dts:201
 *   led-gpio = <&gpio1 3 GPIO_ACTIVE_LOW>（pinctrl: pad 0x10B0，注释即"LED0"）
 * 引脚偏移与 imx6ul-pinfunc.h 同源（mux 0x68 / pad 0x2F4，ALT5）。
 */

#include <types.h>
#include <imx6ull.h>
#include <led.h>

/* 位段（寄存器地址在 imx6ull.h 词典，RM 名） */
#define LED_GPIO_BIT		BIT(3)		/* GPIO1_IO03 */
#define LED_ALT5		5

/**
 * led_init() - 配置 LED0 引脚为 GPIO 输出，初始熄灭
 */
void led_init(void)
{
	writel(readl(CCM_CCGR1) | (3 << 26), CCM_CCGR1);

	writel(LED_ALT5, IOMUXC_SW_MUX_CTL_PAD_GPIO1_IO03);
	writel(0x10B0, IOMUXC_SW_PAD_CTL_PAD_GPIO1_IO03);

	writel(readl(GPIO1_GDIR) | LED_GPIO_BIT, GPIO1_GDIR);
	led_set(0);
}

/**
 * led_set() - 点亮/熄灭 LED0
 * @on: 1 = 点亮（低电平），0 = 熄灭
 */
void led_set(int on)
{
	u32 dr = readl(GPIO1_DR);

	if (on) {
		dr &= ~LED_GPIO_BIT;
	} else {
		dr |= LED_GPIO_BIT;
	}
	writel(dr, GPIO1_DR);
}
