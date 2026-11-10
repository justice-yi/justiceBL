// SPDX-License-Identifier: MIT
/*
 * 启动模式仲裁 —— 消费 Linux reboot-mode 框架留在 SNVS LPGPR 的魔数。
 *
 * 协议（与内核 dts syscon-reboot-mode 的 DT 词表互为约定，两侧魔数
 * 必须一致）：
 *   Linux `reboot loader`   -> LPGPR = 0x4C4F4144 ("LOAD")
 *   Linux `reboot recovery` -> LPGPR = 0x52434F56 ("RCOV")
 *   Linux 普通 `reboot`     -> LPGPR = 0x4E4F524D ("NORM")
 *
 * 接力规则（单字寄存器一棒传到底）：
 *   - 白名单命中才消费，**谁消费谁清零**，不归自己的放行
 *     （LOAD 本模块清零进 USB；RCOV 放行给后续环节——U-Boot boot.scr）；
 *   - NORM/0/垃圾值一律放行正常启动。
 */

#include <types.h>
#include <imx6ull.h>
#include <printf.h>
#include <usbd.h>
#include "bootmode.h"

void bootmode_dispatch(void)
{
	u32 flag = readl(MX6ULL_SNVS_LP_BASE + MX6ULL_SNVS_LPGPR_OFF);

	if (flag != REBOOT_MAGIC_LOADER)
		return;				/* NORM/RCOV/0/垃圾：放行 */

	writel(0, MX6ULL_SNVS_LP_BASE + MX6ULL_SNVS_LPGPR_OFF);
	printf("bootmode: loader caught, cleared\n");
	usbd_download_mode();			/* 不返回 */
}
