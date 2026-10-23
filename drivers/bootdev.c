// SPDX-License-Identifier: MIT
/*
 * justiceBL 启动源识别 —— 读 ROM 锁存的启动配置引脚（SRC_SBMR1），
 * 返回通用块源。介质差异到此为止，上层只见 struct block_source。
 *
 * 判据（RM 第 8 章 + 正点原子拨码表 9.3.2，位号同 U-Boot imx-regs.h:523）：
 *   SBMR1[7:4]  = BOOT_CFG1[7:4]  启动设备族：010x=SD，011x=MMC/eMMC
 *   SBMR1[11]   = BOOT_CFG2[3]    SDHC 实例：0=USDHC1(TF)，1=USDHC2(eMMC)
 * USB/UART 为预留：ROM 的串行下载模式不会运行 justiceBL（ROM 常驻下载器），
 * 这两个槽位是给将来 flashtool 直连输入通道留的接口位。
 */

#include <types.h>
#include <imx6ull.h>
#include <blocksrc.h>
#include <bootdev.h>
#include <stddef.h>

#define SRC_SBMR1		(MX6ULL_SRC_BASE + 0x04)

/* 预留桩：接口占位，未实现 */
const struct block_source usb_source = {
	.name = "USB (reserved for flashtool SDP)",
	.init = NULL,
	.read = NULL,
};

const struct block_source uart_source = {
	.name = "UART (reserved)",
	.init = NULL,
	.read = NULL,
};

const struct block_source *const block_sources[] = {
	&sd_source,
	&emmc_source,
	&usb_source,
	&uart_source,
};
const unsigned int block_source_count =
	sizeof(block_sources) / sizeof(block_sources[0]);

const struct block_source *boot_source_get(void)
{
	u32 cfg = readl(SRC_SBMR1);
	u32 family = (cfg >> 4) & 0xF;		/* BOOT_CFG1[7:4] */
	u32 instance = (cfg >> 11) & 1;		/* BOOT_CFG2[3] */

	if ((family & 0xE) == 0x4 && !instance) {
		return &sd_source;
	}
	if ((family & 0xE) == 0x6 && instance) {
		return &emmc_source;
	}
	return NULL;
}
