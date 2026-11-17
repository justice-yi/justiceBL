// SPDX-License-Identifier: MIT
/*
 * 底板 KEY 按键驱动 —— GPIO1_IO18 输入（硬件依据见 key.h）。
 *
 * 上拉由 pad 配置 0xF080 提供，未按读 1；机械抖动只出现在按下/释放
 * 沿（µs 级），长按判定窗口（秒级）天然免疫，不做软件去抖。
 */

#include <types.h>
#include <imx6ull.h>
#include <key.h>

#define KEY_GPIO_BIT		BIT(18)		/* GPIO1_IO18 */
#define KEY_ALT5		5
#define KEY_PAD_CONFIG		0xF080		/* dts pinctrl_key 原值（含上拉） */

int key_init(void)
{
	/* GPIO1 门控（与 led 同门控，RMW 幂等无害）+ pad + 输入方向 */
	writel(readl(CCM_CCGR1) | (3 << 26), CCM_CCGR1);
	writel(KEY_ALT5, IOMUXC_SW_MUX_CTL_PAD_UART1_CTS_B);
	writel(KEY_PAD_CONFIG, IOMUXC_SW_PAD_CTL_PAD_UART1_CTS_B);
	writel(readl(GPIO1_GDIR) & ~KEY_GPIO_BIT, GPIO1_GDIR);
	return 0;
}

int key_is_pressed(void)
{
	return !(readl(GPIO1_DR) & KEY_GPIO_BIT);
}
