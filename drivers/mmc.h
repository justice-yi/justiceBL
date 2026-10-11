/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_MMC_H
#define _JUSTICEBL_MMC_H

/*
 * SD/MMC 子系统 API：USDHC 主机驱动 + SD/eMMC 卡识别协议（层内分层）。
 * 当前为最小可靠实现：1 位总线、400kHz、PIO 单块读——速度优化留待后续。
 */

#include <types.h>

/** 卡实例：sd_init()/emmc_init() 填充，之后用于块读取 */
struct mmc_card {
	u32 base;		/**< USDHC 基址（模块私有，调用者只当句柄用 */
	u32 rca;		/**< 卡相对地址（识别流程分配） */
	u8 is_emmc;		/**< 1 = eMMC（CMD1 流程），0 = SD（ACMD41 流程） */
	u8 pad[3];
	u32 csd_raw[4];		/**< 原始 CSD（R2 响应原样，调试/容量解析用） */
};

int sd_init(struct mmc_card *card);
int emmc_init(struct mmc_card *card);

/**
 * mmc_read_blocks() - 从卡的用户区读取若干 512 字节块
 * @card:   sd_init/emmc_init 成功后的实例
 * @lba:    起始逻辑块号（块寻址，SDHC/eMMC 均为块单位）
 * @nblocks: 块数
 * @buf:    目标缓冲区（调用者保证尺寸，需 4 字节对齐）
 * @progress: 每块完成后的回调（可为 NULL）：done 为已完成块数，total 为总块数
 *
 * Return: 0 成功；负数错误码（-1 命令超时/错误，-2 CRC 不符）
 */
int mmc_read_blocks(struct mmc_card *card, u32 lba, u32 nblocks, void *buf,
		     void (*progress)(u32 done, u32 total));

#endif /* _JUSTICEBL_MMC_H */
