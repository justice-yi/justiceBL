/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_MMC_H
#define _JUSTICEBL_MMC_H

/*
 * SD/MMC 子系统 API：USDHC 主机驱动 + SD/eMMC 卡识别协议（层内分层）。
 * 当前为最小可靠实现：1 位总线、400kHz、PIO 单块读——速度优化留待后续。
 */

#include <types.h>

/** 卡实例：sd_init()/emmc_init() 填充，之后用于块读取。
 * CID/详情字段位段解码规则逐条对拍 kernel drivers/mmc/core/{sd,mmc}.c
 * （mmc_decode_cid；数组方向已适配：raw[0]=最高字，内核宏按 resp[3] 最高） */
struct mmc_card {
	u32 base;		/**< USDHC 基址（模块私有，调用者只当句柄用 */
	u32 rca;		/**< 卡相对地址（识别流程分配） */
	u8 is_emmc;		/**< 1 = eMMC（CMD1 流程），0 = SD（ACMD41 流程） */
	u8 pad[3];
	u32 csd_raw[4];		/**< CSD 原始位（R2 响应经 r2_deshift 去硬件
				  * 8 位偏移后的规范布局，调试/容量解析用） */
	u32 blocks;		/**< 用户区容量（512B 块数；0 = 未知） */
	u32 cid_raw[4];		/**< CID 原始位（CMD2 响应去移位后，规范布局） */
	/* SD 专属（CSD/SCR 解码，init 时一次填好） */
	u8 tran_speed_mhz;	/**< CSD.TRAN_SPEED 换算（0x32→25、0x5A→50） */
	u8 sd_spec;		/**< SCR.SD_SPEC：0=1.0/1.01 1=1.10 2=2.0/3.0 */
	u8 sd_spec3;		/**< SCR bit47：SD_SPEC=2 时 1=3.0 */
	u8 bus_width_4;		/**< SCR.SD_BUS_WIDTHS bit1（4 位支持） */
	/* eMMC 专属（EXT_CSD 摘录，CMD8 时一次填好） */
	u8 ext_rev;		/**< EXT_CSD[192]：0=4.0…6=5.1 */
	u8 pre_eol;		/**< EXT_CSD[267]：1=Normal 2=Warning 3=Urgent */
	u8 life_a;		/**< EXT_CSD[268]：1..10 档（10%/档），11=超期 */
	u8 life_b;		/**< EXT_CSD[269]：同上（B 分区） */
	u32 cache_kb;		/**< EXT_CSD[249..252]（KB） */
	u32 boot_mult;		/**< EXT_CSD[226]（单位 128KB，双 boot 分区共享） */
	u8 cache_ctrl;		/**< 1 = 写缓存已启用（CMD6 写 EXT_CSD[33]=1） */
};

/** 卡身份/健康详情 → 单行文本（INFO 的 media detail 行；buf 建议≥160B） */
void mmc_card_detail(const struct mmc_card *card, char *buf, u32 size);

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

/**
 * mmc_write_blocks() - 向卡的用户区写入若干 512 字节块（CMD24 逐块）
 * 参数与返回同 mmc_read_blocks；-3 = 卡编程未就绪（CMD13 轮询超时）
 */
int mmc_write_blocks(struct mmc_card *card, u32 lba, u32 nblocks,
		     const void *buf, void (*progress)(u32 done, u32 total));

#endif /* _JUSTICEBL_MMC_H */
