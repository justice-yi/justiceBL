/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_IMX6ULL_H
#define _JUSTICEBL_IMX6ULL_H

/*
 * i.MX6ULL SoC 级定义：外设基址 + 寄存器访问原语。
 * 基址依据 RM 第 2 章（内存映射）；writel/readl 与内核同名同义。
 */

#include <types.h>

#define MX6ULL_UART1_BASE	0x02020000
#define MX6ULL_GPIO1_BASE	0x0209C000
#define MX6ULL_CCM_BASE		0x020C4000
#define MX6ULL_IOMUXC_BASE	0x020E0000

static inline void writel(u32 val, u32 addr)
{
	*(volatile u32 *)addr = val;
}

static inline u32 readl(u32 addr)
{
	return *(volatile u32 *)addr;
}

#endif /* _JUSTICEBL_IMX6ULL_H */
