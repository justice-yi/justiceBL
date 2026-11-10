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
#define MX6ULL_CCM_CACRR_OFF	0x10	/* RM 18.6.2：ARM 时钟分频 ARM_PODF[2:0] */
/* System Counter（SP807）控制块，RM 内存表 "System Counter_CTRL 021D_C000" */
#define MX6ULL_SCTR_BASE	0x021DC000
#define MX6ULL_IOMUXC_BASE	0x020E0000
#define MX6ULL_IOMUXC_MUX_LCD_RESET	0x020E0114	/* RM：SW_MUX_CTL_PAD_LCD_RESET（ALT4=WDOG1_WDOG_ANY） */
#define MX6ULL_GPIO3_BASE	0x020A4000	/* LCD_RESET 的 ALT5 = GPIO3_IO04 */
#define MX6ULL_SRC_BASE		0x020D8000	/* sbmr1=+0x04, imx-regs.h:523 */
#define MX6ULL_WDOG1_BASE	0x020BC000	/* RM 59 章，16 位寄存器按 32 位访问禁 */
/* SNVS LP 域（RM 48 章，电池供电，跨复位保持） */
#define MX6ULL_SNVS_LP_BASE	0x020CC000
#define MX6ULL_SNVS_LPGPR_OFF	0x68		/* RM 48.7.14：32 位通用寄存器，reboot 魔数落点 */

static inline void writel(u32 val, u32 addr)
{
	*(volatile u32 *)addr = val;
}

static inline u32 readl(u32 addr)
{
	return *(volatile u32 *)addr;
}

/* 16 位寄存器（WDOG 等）专用——RM 明令禁 32 位访问，32 位打上去会挂总线 */
static inline void writew(u16 val, u32 addr)
{
	*(volatile u16 *)addr = val;
}

static inline u16 readw(u32 addr)
{
	return *(volatile u16 *)addr;
}

#endif /* _JUSTICEBL_IMX6ULL_H */
