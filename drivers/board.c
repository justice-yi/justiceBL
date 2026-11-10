// SPDX-License-Identifier: MIT
/*
 * justiceBL 板级早期初始化 —— 对应 uboot board 文件的 board_early_init 位置：
 * 加载下一级之前必须拨正的"板级电路"。
 */

#include <types.h>
#include <imx6ull.h>
#include "board.h"

void board_early_init(void)
{
	/*
	 * 拆"看门狗→整板断电"链（核心板 V1.4 硬件，根因分析见
	 * docs/usb-download-mode.md）：LCD_RESET pad 被内核 dts 复用为
	 * WDOG1_WDOG_ANY（EVK 设计：看门狗复位 -> 通知电源芯片冷启动），
	 * WDOG 复位使该输出锁存拉低并保持到 POR——核心板 UM805RE 监视
	 * 芯片据此切核压 buck，实测 ~1.5s 后整板断电（SRSR=0x1）。
	 * 把 pad 改回 GPIO3_IO04 并输出高，释放 UM805RE 的 MR。
	 * 根因修复是内核 dts 删 pinctrl_wdog；此处三笔写兜底"现场仍烧
	 * 旧内核"的过渡期。三笔写放最前（~65ms ≪ 1.5s 断电窗口）。
	 */
	writel(0x5, MX6ULL_IOMUXC_MUX_LCD_RESET);			/* ALT5 GPIO3_IO04 */
	writel(readl(MX6ULL_GPIO3_BASE) | BIT(4), MX6ULL_GPIO3_BASE);	/* DR bit4=1（先置电平） */
	writel(readl(MX6ULL_GPIO3_BASE + 0x4) | BIT(4),
	       MX6ULL_GPIO3_BASE + 0x4);				/* GDIR bit4=1 输出 */
}
