/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_SELFTEST_H
#define _JUSTICEBL_SELFTEST_H

/*
 * 回归自检（纯测试，不含任何驱动初始化职责）：只认通用块源。
 */

#include <blocksrc.h>

void selftest_run(const struct block_source *boot_src);

#endif /* _JUSTICEBL_SELFTEST_H */
