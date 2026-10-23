/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_BOOTDEV_H
#define _JUSTICEBL_BOOTDEV_H

/*
 * 启动源识别：读 ROM 锁存的启动配置（SRC_SBMR1）→ 返回通用块源。
 * 主流程拿到的是 const struct block_source *，不感知具体介质。
 */

#include <blocksrc.h>

enum boot_media {
	BOOT_MEDIA_SD,
	BOOT_MEDIA_EMMC,
	BOOT_MEDIA_USB,			/* 预留：flashtool SDP/USB 通道 */
	BOOT_MEDIA_UART,		/* 预留：串口下载通道 */
	BOOT_MEDIA_UNKNOWN,
};

/**
 * boot_source_get() - 识别当前启动介质并返回其块源
 *
 * Return: 块源指针；未识别返回 NULL。预留介质返回的实例 init 为 NULL，
 * 由调用方决定策略（跳过/提示/挂死）。
 */
const struct block_source *boot_source_get(void);

#endif /* _JUSTICEBL_BOOTDEV_H */
