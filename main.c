// SPDX-License-Identifier: MIT
/*
 * justiceBL 主流程 v0.12 —— 介质无关：识别启动源 -> 初始化 -> [自检] -> 二级启动。
 *
 * 职责划分（全部只认 block_source 接口）：
 *   bootdev  "从哪来"：SRC 寄存器识别 -> 返回块源（SD/eMMC 现役，
 *            USB/UART 预留位）
 *   selftest "好不好"：仅开发调试（SELFTEST 宏开启时编译），不进生产路径
 *   boot     "到哪去"：解析二级镜像头，加载并跳转（不返回）
 *   main     纯编排：启动介质确定后只走该介质的流程
 */

#include <types.h>
#include <uart.h>
#include <printf.h>
#include <timer.h>
#include <blocksrc.h>
#include <bootdev.h>
#include "boot.h"

#ifdef JUSTICEBL_SELFTEST
#include "selftest.h"
#endif

static void halt_with_msg(const char *msg)
{
	printf("%s, halt (wfi)\n", msg);
	for (;;) {
		__asm__ volatile ("wfi");
	}
}

int main(void)
{
	const struct block_source *src;

	uart_init();
	if (timer_init()) {
		halt_with_msg("syscounter not running");
	}
	/* __DATE__ __TIME__ 随每次构建变化——banner 即构建指纹，
	 * 烧录是否生效一眼可辨（v0.10 烧录失效案的教训）。
	 * 主机/账户由 Makefile 注入（BUILD_HOST/BUILD_USER），多机构建可溯源 */
	printf("\njusticeBL %s %s %s@%s\n",
	       __DATE__, __TIME__, BUILD_USER, BUILD_HOST);

	src = boot_source_get();
	if (!src) {
		halt_with_msg("boot media not identified");
	}
	printf("boot source: %s\n", src->name);

	if (!src->init) {
		halt_with_msg("boot source reserved, not implemented");
	}
	record_time("boot src init -");
	if (src->init()) {
		halt_with_msg("boot source init failed");
	}
	record_time("boot src init +");

#ifdef JUSTICEBL_SELFTEST
	selftest_run(src);
#endif

	stage2_boot(src);		/* 不返回 */

	halt_with_msg("stage2 returned");
	return 0;
}
