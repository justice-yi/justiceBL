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
#include <timer.h>
#include <usbd.h>
#include <key.h>
#include "bootmode.h"

/* 长按进 loader 的阈值：按住满此时长才触发（中途松手=正常启动） */
#define KEY_LONG_PRESS_MS	1000

/*
 * 硬件按键路：按住 KEY 上电/复位 → 满足长按 → loader。
 * 与 SNVS 魔数路（reboot loader）并存——软件魔数 + 硬件按键两条入口。
 * 未按下时零开销（一次 GPIO 读），正常启动不增加任何延迟。
 * 计时用 timer_counter 差值（timer_init 在 main 已先跑）；ticks 换算
 * 取 freq/1000 整除（8MHz/1000=8000 精确，避免 u64 除法拉 libgcc）。
 */
static void key_long_press_check(void)
{
	u64 start;
	u64 ticks = (u64)KEY_LONG_PRESS_MS * (timer_freq_hz() / 1000);

	key_init();
	if (!key_is_pressed())
		return;				/* 没按：正常启动 */

	printf("bootmode: key held, %u ms to loader...\n", KEY_LONG_PRESS_MS);
	start = timer_counter();
	while (timer_counter() - start < ticks) {
		if (!key_is_pressed())
			return;			/* 中途松手：正常启动 */
	}
	printf("bootmode: key long-press -> loader\n");
	usbd_download_mode();			/* 不返回 */
}

void bootmode_dispatch(void)
{
	u32 flag;

	key_long_press_check();

	flag = readl(SNVS_LPGPR);
	if (flag != REBOOT_MAGIC_LOADER)
		return;				/* NORM/RCOV/0/垃圾：放行 */

	writel(0, SNVS_LPGPR);
	printf("bootmode: loader caught, cleared\n");
	usbd_download_mode();			/* 不返回 */
}
