// SPDX-License-Identifier: MIT
/*
 * justiceBL 回归自检（介质无关，只操作 block_source 接口）：
 *   1. selftest_readback()  —— 从启动源 LBA2 读回 justiceBL 自身镜像，
 *      与 ROM 搬进 RAM 的副本逐字节比对 + CRC32
 *   2. selftest_others()    —— 遍历其余已注册源，逐个探测
 * 自检只体检不初始化主业；失败仅报告，不阻断启动。
 */

#include <types.h>
#include <uart.h>
#include <blocksrc.h>
#include <bootdev.h>		/* block_sources 表 */
#include <crc32.h>
#include <stddef.h>
#include "selftest.h"

/* justiceBL 镜像在 RAM 的起点（= ROM 搬运后的 IVT 落点，mkimx HDR=0xC00 布局）*/
#define IMAGE_BASE		0x00907400U
/* 读回缓冲：栈顶之上、OCRAM 空闲区顶 0x00918000 之下（镜像 < ~44KB 时安全）*/
#define READ_BUF		((u8 *)0x0090C000)

extern char __bss_start[];		/* 链接符号：文件镜像末端 */

static void progress_dot(u32 done, u32 total)
{
	(void)done;
	(void)total;
	uart_putc('.');
}

static void selftest_report(const char *tag, int ok)
{
	uart_puts(tag);
	uart_puts(ok ? " OK\n" : " FAIL\n");
}

static void selftest_readback(const struct block_source *src)
{
	u32 image_len = (u32)__bss_start - IMAGE_BASE;
	u32 nblocks = (image_len + 511) / 512;
	u32 crc;

	if (src->read(2, nblocks, READ_BUF, progress_dot)) {
		selftest_report("readback:", 0);
		return;
	}
	crc = crc32(READ_BUF, image_len);
	uart_puts("readback: CRC32=");
	uart_puthex(crc);
	uart_puts(" RAM ");
	for (u32 i = 0; i < image_len; i++) {
		if (READ_BUF[i] != ((const u8 *)IMAGE_BASE)[i]) {
			selftest_report("compare:", 0);
			return;
		}
	}
	selftest_report("compare:", 1);
}

/* 遍历除启动源之外的已注册源：init 为 NULL 打"预留"，否则试读一块 */
static void selftest_others(const struct block_source *boot_src)
{
	u8 blk[512] __attribute__((aligned(4)));

	for (unsigned int i = 0; i < block_source_count; i++) {
		const struct block_source *src = block_sources[i];

		if (src == boot_src) {
			continue;
		}
		uart_puts("probe ");
		uart_puts(src->name);
		uart_puts(": ");
		if (!src->init) {
			uart_puts("reserved\n");
			continue;
		}
		if (src->init() || src->read(2, 1, blk, NULL)) {
			uart_puts("no card\n");
			continue;
		}
		uart_puts("LBA2=");
		uart_puthex(*(u32 *)blk);
		uart_putc('\n');
	}
}

void selftest_run(const struct block_source *boot_src)
{
	selftest_readback(boot_src);
	selftest_others(boot_src);
}
