// SPDX-License-Identifier: MIT
/*
 * justiceBL 二级启动 —— 从块源加载 U-Boot 裸镜像并跳转（不返回）。
 *
 * U-Boot 不带 .imx 头（IVT/BootData/DCD 的职责已由 justiceBL 承接：
 * 入口/加载点在本模块配置，DDR 由 justiceBL 自身镜像头的 DCD 初始化），
 * 存储即裸 u-boot-dtb.bin——这正是"U-Boot 可作为普通分区直接烧录"的形态。
 *
 * 加载参数集中于 STAGE2 配置块；量产接入 layout.csv 的 uboot 分区时
 * 只改这里，流程不动。
 */

#include <types.h>
#include <stddef.h>
#include <uart.h>
#include <blocksrc.h>
#include "boot.h"

/* ---- stage2 配置（唯一事实源）---- */
#define STAGE2_LBA		64	/* 开发期：1MB 裸区间隙内、justiceBL
					 * （LBA2..~20）之后的对齐位置；
					 * 量产：换 layout.csv uboot 分区起始 */
#define STAGE2_LOAD_ADDR	0x87800000	/* = CONFIG_SYS_TEXT_BASE */
#define STAGE2_ENTRY		STAGE2_LOAD_ADDR /* 裸 bin 首指令即入口 */
#define STAGE2_MAX_BLOCKS	1200	/* 上限 600KB（现役 U-Boot ~547KB）；
					 * 越界读入的是卡上填充，落在 U-Boot
					 * 的 bss/未用区，其启动代码自会清 bss */

static void progress_dot(u32 done, u32 total)
{
	(void)total;
	if (!(done & 0x3F)) {		/* 每 64 块一个点 */
		uart_putc('.');
	}
}

/**
 * stage2_jump() - 跳转到二级镜像入口（不返回）
 */
static void stage2_jump(u32 entry)
{
	void (*fn)(void) = (void (*)(void))entry;

	uart_puts("\nstage2: jumping to ");
	uart_puthex(entry);
	uart_puts("\n");
	fn();
}

void stage2_boot(const struct block_source *src)
{
	static u8 blk[512] __attribute__((aligned(4)));
	u8 *dest = (u8 *)STAGE2_LOAD_ADDR;

	/* 首块先验货：U-Boot 裸 bin 首指令是 ARM 分支（高字节 0xEA/0xE5），
	 * 用它排除"该位置没有有效镜像"（空白/0xFF/错位）*/
	if (src->read(STAGE2_LBA, 1, blk, NULL) ||
	    (blk[3] != 0xEA && blk[3] != 0xE5)) {
		uart_puts("stage2: no image at LBA ");
		uart_puthex(STAGE2_LBA);
		uart_puts(", halt\n");
		for (;;) {
			__asm__ volatile ("wfi");
		}
	}
	uart_puts("stage2: load ");
	uart_puthex(STAGE2_MAX_BLOCKS);
	uart_puts(" blocks from LBA ");
	uart_puthex(STAGE2_LBA);
	uart_puts(" -> ");
	uart_puthex(STAGE2_LOAD_ADDR);
	uart_putc('\n');

	for (u32 i = 0; i < STAGE2_MAX_BLOCKS; i++) {
		if (i == 0) {			/* 首块已在手 */
			for (u32 j = 0; j < 512; j++) {
				dest[j] = blk[j];
			}
			progress_dot(1, STAGE2_MAX_BLOCKS);
			continue;
		}
		if (src->read(STAGE2_LBA + i, 1, dest + i * 512,
			      progress_dot)) {
			uart_puts("\nstage2: read fail, halt\n");
			for (;;) {
				__asm__ volatile ("wfi");
			}
		}
	}

	uart_puts("\n");
	stage2_jump(STAGE2_ENTRY);
}
