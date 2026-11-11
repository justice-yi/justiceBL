/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_BLOCKSRC_H
#define _JUSTICEBL_BLOCKSRC_H

/*
 * 通用块源抽象 —— 主流程（main/selftest/boot）只认这个接口，
 * 不感知任何具体介质（SD/eMMC/USB/UART）。kernel ops 风格回调。
 *
 * 新介质接入 = 实现一个 const struct block_source 并加进 sources 表，
 * 主流程与其余模块零改动。
 */

#include <types.h>

/**
 * struct block_source - 一个可读取块数据的启动/输入介质
 * @name:	人读名（打印用）
 * @init:	初始化该源；0 成功。NULL = 预留未实现
 * @read:	读 nblocks 个 512B 块到 buf；0 成功。progress 可为 NULL
 * @write:	把 buf 的 nblocks 个 512B 块写到 lba；0 成功。
 *		NULL = 只读介质（如预留的串口下载通道）
 * @blocks:	用户区容量（512B 块数）；NULL = 未知
 */
struct block_source {
	const char *name;
	int (*init)(void);
	int (*read)(u32 lba, u32 nblocks, void *buf,
		    void (*progress)(u32 done, u32 total));
	int (*write)(u32 lba, u32 nblocks, const void *buf,
		     void (*progress)(u32 done, u32 total));
	u32 (*blocks)(void);
};

/* 已注册的全部块源（索引即自检遍历顺序，bootdev.c 定义）*/
extern const struct block_source *const block_sources[];
extern const unsigned int block_source_count;

/* 各介质实例：mmc.c 提供 SD/eMMC；bootdev.c 提供 USB/UART 预留桩 */
extern const struct block_source sd_source;
extern const struct block_source emmc_source;
extern const struct block_source usb_source;	/* 预留：flashtool SDP/USB */
extern const struct block_source uart_source;	/* 预留：串口下载通道 */

#endif /* _JUSTICEBL_BLOCKSRC_H */
