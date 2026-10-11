// SPDX-License-Identifier: MIT
/*
 * justiceBL 应用层 v0.5 —— SD/eMMC 驱动验证：把自己从 TF 卡读回来校验。
 *
 * 自检流程（结果全部走串口）：
 *   1. sd_init  识别 TF 卡（USDHC1）
 *   2. 从 LBA2（镜像在卡上的位置）读回整个 justiceBL.imx
 *      —— 与 ROM 已搬进 RAM 的副本（IMAGE_BASE 起）逐字节比对 + 算 CRC32
 *   3. emmc_init 识别 eMMC（USDHC2），读 LBA2 验证 U-Boot IVT 头（0xD1 标签）
 *   4. 打印总结后 wfi 待机
 */

#include <types.h>
#include <uart.h>
#include <mmc.h>
#include <crc32.h>
#include <stddef.h>

/* 镜像在 RAM 的起点 = ROM 搬运后的 IVT 落点（对应 mkimx.py 的 HDR=0xC00 布局）*/
#define IMAGE_BASE		0x00907400U
/* 整卡读回缓冲：栈顶之上、OCRAM 空闲区顶 0x00918000 之下（约束：镜像 < ~44KB）*/
#define READ_BUF		((u8 *)0x0090C000)

/* 链接脚本符号：.bss 起点即文件镜像末端（bin 大小不含 NOLOAD 段）*/
extern char __bss_start[];

static u8 block_buf[512] __attribute__((aligned(4)));

static void report(const char *tag, int code)
{
	uart_puts(tag);
	if (code) {
		uart_puts(" FAIL(");
		uart_puthex((u32)code);
		uart_puts(")\n");
	} else {
		uart_puts(" OK\n");
	}
}

/** 逐块进度回调：每块打一个点（mmc_read_blocks 的 done/total 也可用于百分比）*/
static void progress_dot(u32 done, u32 total)
{
	(void)total;
	(void)done;
	uart_putc('.');
}

static int block_eq(const u8 *a, const u8 *b, u32 len)
{
	for (u32 i = 0; i < len; i++) {
		if (a[i] != b[i]) {
			return 0;
		}
	}
	return 1;
}

static void hexdump_line(const u8 *p, u32 n)
{
	static const char hex[] = "0123456789abcdef";

	for (u32 i = 0; i < n; i++) {
		uart_putc(hex[p[i] >> 4]);
		uart_putc(hex[p[i] & 0xf]);
		uart_putc(' ');
	}
	uart_putc('\n');
}

/** 卡侧数据经 32 位字节交换后是否与 RAM 一致（端序诊断）*/
static int match_swab32(const u8 *card, const u8 *ram, u32 len)
{
	for (u32 i = 0; i + 4 <= len; i += 4) {
		if (card[i] != ram[i + 3] || card[i + 1] != ram[i + 2] ||
		    card[i + 2] != ram[i + 1] || card[i + 3] != ram[i]) {
			return 0;
		}
	}
	return 1;
}

/** 卡侧数据经 16 位半字交换后是否与 RAM 一致（端序诊断）*/
static int match_swap16(const u8 *card, const u8 *ram, u32 len)
{
	for (u32 i = 0; i + 4 <= len; i += 4) {
		if (card[i] != ram[i + 2] || card[i + 1] != ram[i + 3] ||
		    card[i + 2] != ram[i] || card[i + 3] != ram[i + 1]) {
			return 0;
		}
	}
	return 1;
}

static void self_read_verify(void)
{
	struct mmc_card sd;
	u32 image_len = (u32)__bss_start - IMAGE_BASE;
	u32 nblocks = (image_len + 511) / 512;
	u32 crc;

	uart_puts("image in RAM: ");
	uart_puthex(IMAGE_BASE);
	uart_puts(" len ");
	uart_puthex(image_len);
	uart_puts(" (");
	uart_puthex(nblocks);
	uart_puts(" blocks)\n");

	if (sd_init(&sd)) {
		report("SD identify", -1);
		return;
	}
	uart_puts("SD ready, RCA=0x");
	uart_puthex(sd.rca);
	uart_puts("\nread back self-image: ");

	if (mmc_read_blocks(&sd, 2, nblocks, READ_BUF, progress_dot)) {
		report("\nSD read", -2);
		return;
	}
	uart_puts(" done\n");

	crc = crc32(READ_BUF, image_len);
	uart_puts("CRC32(card) = 0x");
	uart_puthex(crc);
	uart_puts("  vs RAM copy: ");
	if (block_eq(READ_BUF, (const u8 *)IMAGE_BASE, image_len)) {
		uart_puts("MATCH\n");
		return;
	}
	uart_puts("MISMATCH!\n");
	uart_puts("card[0..31] : ");
	hexdump_line(READ_BUF, 32);
	uart_puts("ram [0..31] : ");
	hexdump_line((const u8 *)IMAGE_BASE, 32);
	if (match_swab32(READ_BUF, (const u8 *)IMAGE_BASE, image_len)) {
		uart_puts("=> full match under 32-bit byteswap (PROT_CTRL EMODE)\n");
	} else if (match_swap16(READ_BUF, (const u8 *)IMAGE_BASE, image_len)) {
		uart_puts("=> full match under 16-bit halfword swap (PROT_CTRL EMODE)\n");
	} else {
		uart_puts("=> not an endian transform; first diff: ");
		for (u32 i = 0; i < image_len; i++) {
			if (READ_BUF[i] != ((const u8 *)IMAGE_BASE)[i]) {
				uart_puthex(i);
				uart_puts("\n");
				break;
			}
		}
	}
}

static void emmc_probe(void)
{
	struct mmc_card emmc;

	if (emmc_init(&emmc)) {
		report("eMMC identify", -1);
		return;
	}
	uart_puts("eMMC ready, RCA=0x");
	uart_puthex(emmc.rca);
	uart_puts("\neMMC LBA2: ");

	if (mmc_read_blocks(&emmc, 2, 1, block_buf, NULL)) {
		report("read", -2);
		return;
	}
	uart_puthex(*(u32 *)block_buf);
	if ((block_buf[0] == 0xD1) && (block_buf[2] == 0x20)) {
		uart_puts(" (U-Boot IVT tag 0xD1 found)\n");
	} else {
		uart_puts(" (no IVT tag)\n");
	}
}

int main(void)
{
	uart_init();
	uart_puts("\njusticeBL v0.5: SD/eMMC drivers + self-read verify\n");

	self_read_verify();
	emmc_probe();

	uart_puts("selftest done, halting (wfi)\n");
	for (;;) {
		__asm__ volatile ("wfi");
	}
}
