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

#define GPIO_REG(off)		(MX6ULL_GPIO1_BASE + (off))
#define GPIO_DR_OFF		0x00
#define GPIO_GDIR_OFF		0x04

#define IOMUXC_REG(off)		(MX6ULL_IOMUXC_BASE + (off))
#define MUX_GPIO1_IO03_OFF	0x68
#define PAD_GPIO1_IO03_OFF	0x2F4

#define LED_GPIO_BIT		BIT(3)		/* GPIO1_IO03 */
#define LED_ALT5		5

/* CCM：gpio1 时钟门控 CCGR1 CG13(bit27:26)（clk-imx6ul.c:374） */
#define CCGR1_REG		(MX6ULL_CCM_BASE + 0x6C)

/**
 * led_init() - 配置 LED0 引脚为 GPIO 输出，初始熄灭
 */
void led_init(void)
{
	writel(readl(CCGR1_REG) | (3 << 26), CCGR1_REG);

	writel(LED_ALT5, IOMUXC_REG(MUX_GPIO1_IO03_OFF));
	writel(0x10B0, IOMUXC_REG(PAD_GPIO1_IO03_OFF));

	writel(readl(GPIO_REG(GPIO_GDIR_OFF)) | LED_GPIO_BIT,
	       GPIO_REG(GPIO_GDIR_OFF));
	led_set(0);
}

/**
 * led_set() - 点亮/熄灭 LED0
 * @on: 1 = 点亮（低电平），0 = 熄灭
 */
void led_set(int on)
{
	u32 dr = readl(GPIO_REG(GPIO_DR_OFF));

	if (on) {
		dr &= ~LED_GPIO_BIT;
	} else {
		dr |= LED_GPIO_BIT;
	}
	writel(dr, GPIO_REG(GPIO_DR_OFF));
}
