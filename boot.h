/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_BOOT_H
#define _JUSTICEBL_BOOT_H

/*
 * 二级启动：从任意块源加载 U-Boot 并跳转（不返回）。介质无关。
 */

#include <blocksrc.h>

void stage2_boot(const struct block_source *src);

#endif /* _JUSTICEBL_BOOT_H */
