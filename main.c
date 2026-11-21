// SPDX-License-Identifier: MIT
/*
 * justiceBL 主流程 v0.15 —— 纯编排，无寄存器操作（架构规矩见
 * ~/.zcode/AGENTS.md 与 docs/CODING_STYLE.md）。
 *
 * 职责划分：
 *   board    "拨电路"：板级早期初始化（看门狗签名=三只狗掉电计数器清零，board.c）
 *   bootmode "回哪去"：消费 reboot-mode 魔数，loader -> USB 下载（bootmode.c）
 *   bootdev  "从哪来"：SRC 寄存器识别 -> 返回块源（SD/eMMC 现役，
 *            USB/UART 预留位）
 *   selftest "好不好"：仅开发调试（SELFTEST 宏开启时编译），不进生产路径
 *   boot     "到哪去"：解析二级镜像头，加载并跳转（不返回）
 *   main     纯编排：init 调用 + 流程分发，不碰任何寄存器
 */

#include <types.h>
#include <uart.h>
#include <printf.h>
#include <timer.h>
#include <blocksrc.h>
#include <bootdev.h>
#include <board.h>
#include <bootmode.h>
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

/* ---------- 异常处理器（start.S 向量表跳入，非编排代码） ----------
 * 有声死替代无声死：出错指令地址 + 出错访问地址 + 故障类型全打出来
 * 再挂死。DFSR/IFSR 编码见 ARMv7 ARM B4.9.10（对齐/权限/外部/翻译 等） */

void data_abort_dump(u32 pc, u32 spsr, u32 dfar, u32 dfsr)
{
	printf("\n*** DATA ABORT ***\n");
	printf("  PC   = 0x%08x  (出错指令地址)\n", pc);
	printf("  DFAR = 0x%08x  (出错访问的地址)\n", dfar);
	printf("  DFSR = 0x%08x  (故障类型编码)\n", dfsr);
	printf("  SPSR = 0x%08x  (处理器状态)\n", spsr);
	printf("******************\n");
	for (;;)
		;
}

void prefetch_abort_dump(u32 pc, u32 spsr, u32 ifar, u32 ifsr)
{
	printf("\n*** PREFETCH ABORT ***\n");
	printf("  PC   = 0x%08x  (取指失败地址)\n", pc);
	printf("  IFAR = 0x%08x  (取指访问的地址)\n", ifar);
	printf("  IFSR = 0x%08x  (故障类型编码)\n", ifsr);
	printf("  SPSR = 0x%08x  (处理器状态)\n", spsr);
	printf("**********************\n");
	for (;;)
		;
}

int main(void)
{
	const struct block_source *src;

	board_early_init();

	uart_init();
	if (timer_init()) {
		halt_with_msg("syscounter not running");
	}
	/* __DATE__ __TIME__ 随每次构建变化——banner 即构建指纹，
	 * 烧录是否生效一眼可辨（v0.10 烧录失效案的教训）。
	 * 主机/账户由 Makefile 注入（BUILD_HOST/BUILD_USER），多机构建可溯源 */
	printf("\njusticeBL %s %s %s@%s\n",
	       __DATE__, __TIME__, BUILD_USER, BUILD_HOST);

	bootmode_dispatch();		/* loader 魔数 -> USB 下载模式（不返回） */

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
