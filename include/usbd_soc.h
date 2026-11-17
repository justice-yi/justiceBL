/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_USBD_SOC_H
#define _JUSTICEBL_USBD_SOC_H

/*
 * USB 设备控制器的 SoC 专属层 —— 时钟/PHY 上电等"带时序的 SoC 代码"。
 *
 * 耦合规则：寄存器地址在 imx6ull.h 词典（USB1_* 等），引擎 usbd.c 与
 * 本文件都直引词典（规矩见 SDK AGENTS.md"寄存器定义规矩"）；本文件只
 * 收纳需要多步时序的 SoC 上电序列。（历史注：曾有偏移映射层求引擎跨
 * SoC 复用，按 YAGNI 降级，真复用时再抽。）
 * 本实现针对 i.MX6ULL（ChipIdea IP），依据均为 RM 章节号，见 usbd_soc.c 内注释。
 */

#include <types.h>

/* 时钟：PLL3(USB1 PLL 480MHz) + CCGR6 CG0(usboh3) + 断 Linux 遗留的 PLL_USB2。
 * 幂等，返回 0 成功 */
int usbd_soc_clock_init(void);

/* PHY：USBPHY1 解复位/解时钟门控/解除全部断电位 + 3P0 LDO 切正常稳压
 * （RM 39.4 要求，四方软件都漏的步骤）+ 禁过流/充电检测 */
void usbd_soc_phy_init(void);

#endif /* _JUSTICEBL_USBD_SOC_H */
