/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_USBD_SOC_H
#define _JUSTICEBL_USBD_SOC_H

/*
 * USB 设备控制器的 SoC 专属层 —— 全仓库唯一出现 i.MX 具体地址的 USB 文件。
 *
 * 耦合规则：usbd.c（引擎）只通过本头文件的接口与硬件对话，
 * 使引擎可以在不同 SoC 上复用（换芯片 = 换一个 usbd_soc.c 实现）。
 * 本实现针对 i.MX6ULL（ChipIdea IP），依据均为 RM 章节号，见 usbd_soc.c 内注释。
 */

#include <types.h>

/* ChipIdea 控制器（USB OTG1 core）基址——寄存器偏移表在 usbd.c（IP 级常量） */
u32 usbd_soc_core_base(void);

/* 寄存器访问原语：引擎一律经由这两个函数，不直接解引用地址 */
u32 usbd_soc_rd32(u32 off);
void usbd_soc_wr32(u32 off, u32 val);

/* 时钟：PLL3(USB1 PLL 480MHz) + CCGR6 CG0(usboh3) + 断 Linux 遗留的 PLL_USB2。
 * 幂等，返回 0 成功 */
int usbd_soc_clock_init(void);

/* PHY：USBPHY1 解复位/解时钟门控/解除全部断电位 + 3P0 LDO 切正常稳压
 * （RM 39.4 要求，四方软件都漏的步骤）+ 禁过流/充电检测 */
void usbd_soc_phy_init(void);

#endif /* _JUSTICEBL_USBD_SOC_H */
