// SPDX-License-Identifier: MIT
/*
 * justiceBL 板级早期初始化 —— 对应 uboot board 文件的 board_early_init 位置：
 * 加载下一级之前必须拨正的"板级电路"。
 */

#include <types.h>
#include <imx6ull.h>
#include "board.h"

/*
 * 看门狗签名 —— board_early_init 的第一件事。
 *
 * 契约（RM 59.5.3）：任意复位后，三只狗的 16s 掉电计数器默认武装；
 * 16s 内无人写 WMCR.PDE=0 → 拉 B 一拍 → ANY=OR(B1,B2,B3) → UM805RE
 * → 整板断电（下次开机 SRSR=0x1 POR）。U-Boot（arch_cpu_init →
 * imx_wdog_disable_powerdown）与 Linux（imx2_wdt probe）都在各自入口
 * 签名；justiceBL 作为接管者补上同款签名。
 *
 * 签名的救援语义：justiceBL 自身若在此写之前损坏（坏镜像/早期挂死），
 * 签名缺失 → 16s 计数器开火 → 断电恢复——硬件救援精确覆盖"接管者
 * 已死"的场景，本函数不做任何绕过（v0.15 方案变更：取代旧三笔 pad
 * 写，讨论记录 2026-11-14）。
 *
 * 门控：三只狗的 ipg 时钟在 CCGR3/5/6（clk-imx6ul.c 同位），先全开再写；
 * 写后回读校验，非 0 重试一轮（门控刚开的同步窗口兜底）。回读仍失败
 * 无控制台可报——现象即日志：板子会在 16s 后断电，按设计勿掩耳。
 */
static void wdog_sign_takeover(void)
{
	writel(readl(CCM_CCGR3) | (3 << 16), CCM_CCGR3);		/* WDOG1 */
	writel(readl(CCM_CCGR5) | (3 << 10), CCM_CCGR5);		/* WDOG2 */
	writel(readl(CCM_CCGR6) | (3 << 20), CCM_CCGR6);		/* WDOG3 */

	writew(0, WDOG1_WMCR);
	writew(0, WDOG2_WMCR);
	writew(0, WDOG3_WMCR);

	if (readw(WDOG1_WMCR) | readw(WDOG2_WMCR) | readw(WDOG3_WMCR)) {
		/* 重试一轮：门控刚开的同步窗口兜底 */
		writew(0, WDOG1_WMCR);
		writew(0, WDOG2_WMCR);
		writew(0, WDOG3_WMCR);
	}
}

void board_early_init(void)
{
	wdog_sign_takeover();
}
