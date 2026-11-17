/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_IMX6ULL_H
#define _JUSTICEBL_IMX6ULL_H

/*
 * i.MX6ULL SoC 寄存器词典（唯一入口，规矩见 SDK 根 AGENTS.md"寄存器定义规矩"）
 *
 *  - 名字 = RM 寄存器名，代数取确切实例（USB1、USDHC1/2…），禁 x/n 通配；
 *  - 全表按地址升序，IP 块间分界线标注基址与 RM 章节——本文件随寄存器
 *    增加自然攒成整颗 SoC 的寄存器地图；
 *  - 只收录 justiceBL 用到的寄存器（含 SET/CLR 副本）；位段/掩码宏归驱动；
 *  - writel/readl 与内核同名同义；16 位寄存器（WDOG）只经 writew/readw。
 */

#include <types.h>

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

/* ============ 0x0202_0000 UART1（RM 54 章）============ */
#define UART1_UTXD			0x02020040
#define UART1_UCR1			0x02020080
#define UART1_UCR2			0x02020084
#define UART1_UCR3			0x02020088
#define UART1_UFCR			0x02020090
#define UART1_USR2			0x02020098
#define UART1_UBIR			0x020200A4
#define UART1_UBMR			0x020200A8

/* ============ 0x0209_C000 GPIO1（RM 26 章）============ */
#define GPIO1_DR			0x0209C000
#define GPIO1_GDIR			0x0209C004

/* ============ 0x020B_C000 WDOG1（RM 59 章，16 位访问）============ */
#define WDOG1_WMCR			0x020BC008	/* 59.7.3：PDE=bit0 */

/* ============ 0x020C_0000 WDOG2（RM 59 章，TZ 域，16 位访问）============ */
#define WDOG2_WMCR			0x020C0008

/* ============ 0x020C_4000 CCM（RM 18 章）============ */
#define CCM_CACRR			0x020C4010	/* 18.6.2：ARM_PODF[2:0] */
#define CCM_CSCMR1			0x020C401C	/* usdhc 时钟源 mux */
#define CCM_CSCDR1			0x020C4024	/* usdhc1/2 podf */
#define CCM_CCGR1			0x020C406C	/* GPIO1 门控 bit27:26 */
#define CCM_CCGR3			0x020C4074	/* WDOG1 门控 bit17:16 */
#define CCM_CCGR5			0x020C407C	/* UART1 bit25:24 / WDOG2 bit11:10 / SCTR bit3:2 */
#define CCM_CCGR6			0x020C4080	/* usboh3 CG0 / usdhc1 CG1 / usdhc2 CG2 */

/* ============ 0x020C_8000 CCM_ANALOG（RM 18.7）+ PMU（RM 39）============ */
#define CCM_ANALOG_PLL_USB1		0x020C8010	/* 18.7.2：USB1 480M PLL */
#define CCM_ANALOG_PLL_USB1_SET		0x020C8014
#define CCM_ANALOG_PLL_USB1_CLR		0x020C8018
#define CCM_ANALOG_PLL_USB2_CLR		0x020C8028	/* 18.7.3：CLR=@+0x28
							 * （踩坑：旧码写 0x38——那是
							 * PLL_SYS_CLR，RM 18.7.4；
							 * "断遗留 PLL_USB2"从未生效
							 * 还误伤 PLL2，2026-11-15 修）*/
#define PMU_REG_3P0_SET			0x020C8124	/* 39.6.2：USB PHY 3P0 LDO */
#define PMU_REG_3P0_CLR			0x020C8128
#define USB1_CHRG_DETECT		0x020C81B0	/* 57.4.2：充电检测器 */

/* ============ 0x020C_9000 USBPHY1（RM 57 章）============ */
#define USBPHY1_PWD			0x020C9000	/* 57.3：PWD 复位值 001E_1C00 全断电 */
#define USBPHY1_CTRL_SET		0x020C9034	/* SFTRST/CLKGATE/ENUTMILEVEL */
#define USBPHY1_CTRL_CLR		0x020C9038

/* ============ 0x020C_C000 SNVS LP 域（RM 48 章，电池供电跨复位）============ */
#define SNVS_LPGPR			0x020CC068	/* 48.7.14：32 位通用寄存器 */

/* ============ 0x020D_8000 SRC（Boot Mode Register）============ */
#define SRC_SBMR1			0x020D8004	/* ROM 锁存的启动配置引脚 */

/* ============ 0x020E_0000 IOMUXC（RM 32 章）============ */
/* 引脚名/偏移与 kernel dts imx6ul-pinfunc.h 逐条同源 */
#define IOMUXC_SW_MUX_CTL_PAD_GPIO1_IO03	0x020E0068	/* ALT5=GPIO1_IO03 */
#define IOMUXC_SW_MUX_CTL_PAD_UART1_TX_DATA	0x020E0084	/* ALT0=UART1_TX */
#define IOMUXC_SW_MUX_CTL_PAD_UART1_RX_DATA	0x020E0088	/* ALT0=UART1_RX */
#define IOMUXC_SW_MUX_CTL_PAD_UART1_CTS_B	0x020E008C	/* ALT5=GPIO1_IO18（KEY 按键） */
#define IOMUXC_SW_MUX_CTL_PAD_LCD_RESET		0x020E0114	/* ALT4=WDOG1_WDOG_ANY */
#define IOMUXC_SW_MUX_CTL_PAD_NAND_RE_B		0x020E0178	/* ALT1=USDHC2_CLK */
#define IOMUXC_SW_MUX_CTL_PAD_NAND_WE_B		0x020E017C	/* ALT1=USDHC2_CMD */
#define IOMUXC_SW_MUX_CTL_PAD_NAND_DATA00	0x020E0180	/* ALT1=USDHC2_DATA0 */
#define IOMUXC_SW_MUX_CTL_PAD_NAND_DATA01	0x020E0184	/* ALT1=USDHC2_DATA1 */
#define IOMUXC_SW_MUX_CTL_PAD_NAND_DATA02	0x020E0188	/* ALT1=USDHC2_DATA2 */
#define IOMUXC_SW_MUX_CTL_PAD_NAND_DATA03	0x020E018C	/* ALT1=USDHC2_DATA3 */
#define IOMUXC_SW_MUX_CTL_PAD_SD1_CMD		0x020E01BC	/* ALT0=USDHC1_CMD */
#define IOMUXC_SW_MUX_CTL_PAD_SD1_CLK		0x020E01C0	/* ALT0=USDHC1_CLK */
#define IOMUXC_SW_MUX_CTL_PAD_SD1_DATA0		0x020E01C4	/* ALT0=USDHC1_DATA0 */
#define IOMUXC_SW_MUX_CTL_PAD_SD1_DATA1		0x020E01C8	/* ALT0=USDHC1_DATA1 */
#define IOMUXC_SW_MUX_CTL_PAD_SD1_DATA2		0x020E01CC	/* ALT0=USDHC1_DATA2 */
#define IOMUXC_SW_MUX_CTL_PAD_SD1_DATA3		0x020E01D0	/* ALT0=USDHC1_DATA3 */
#define IOMUXC_SW_PAD_CTL_PAD_GPIO1_IO03	0x020E02F4
#define IOMUXC_SW_PAD_CTL_PAD_UART1_TX_DATA	0x020E0310	/* dts pad 0x1b0b1 */
#define IOMUXC_SW_PAD_CTL_PAD_UART1_RX_DATA	0x020E0314
#define IOMUXC_SW_PAD_CTL_PAD_UART1_CTS_B	0x020E0318	/* KEY pad 0xF080（dts 原值） */
#define IOMUXC_SW_PAD_CTL_PAD_NAND_RE_B		0x020E0404
#define IOMUXC_SW_PAD_CTL_PAD_NAND_WE_B		0x020E0408
#define IOMUXC_SW_PAD_CTL_PAD_NAND_DATA00	0x020E040C
#define IOMUXC_SW_PAD_CTL_PAD_NAND_DATA01	0x020E0410
#define IOMUXC_SW_PAD_CTL_PAD_NAND_DATA02	0x020E0414
#define IOMUXC_SW_PAD_CTL_PAD_NAND_DATA03	0x020E0418
#define IOMUXC_SW_PAD_CTL_PAD_SD1_CMD		0x020E0448
#define IOMUXC_SW_PAD_CTL_PAD_SD1_CLK		0x020E044C
#define IOMUXC_SW_PAD_CTL_PAD_SD1_DATA0		0x020E0450
#define IOMUXC_SW_PAD_CTL_PAD_SD1_DATA1		0x020E0454
#define IOMUXC_SW_PAD_CTL_PAD_SD1_DATA2		0x020E0458
#define IOMUXC_SW_PAD_CTL_PAD_SD1_DATA3		0x020E045C

/* ============ 0x0218_4000 USB1（RM 56 章，ChipIdea 设备核心）============ */
#define USB1_BASE			0x02184000	/* OTG1 核心 */
#define USB1_USBCMD			0x02184140	/* 56.6.18：RS=1 上拉 D+ / RST 软复位 */
#define USB1_USBSTS			0x02184144	/* 56.6.x：URI/UI/PCI 事件（W1C） */
#define USB1_USBINTR			0x02184148	/* 中断使能（本实现轮询，全屏蔽） */
#define USB1_DEVICEADDR			0x02184154	/* 56.6.23：USBADR[31:25]/USBADRA 暂存 */
#define USB1_ENDPTLISTADDR		0x02184158	/* 56.6.25：dQH 列表基址（64 对齐） */
#define USB1_PORTSC1			0x02184184	/* 56.6.31：端口速度/复位状态 */
#define USB1_USBMODE			0x021841A8	/* 56.6.33：CM[1:0]=设备模式，复位后只能写一次 */
#define USB1_ENDPTSETUPSTAT		0x021841AC	/* 56.6.34：setup 收到标志（W1C） */
#define USB1_ENDPTPRIME			0x021841B0	/* 56.6.35：武装传输 */
#define USB1_ENDPTFLUSH			0x021841B4	/* 56.6.36：作废挂起传输 + 复位 toggle */
#define USB1_ENDPTSTAT			0x021841B8	/* 56.6.37：prime 就绪标志 */
#define USB1_ENDPTCOMPLETE		0x021841BC	/* 56.6.38：传输完成事件（W1C） */
#define USB1_ENDPTCTRL0			0x021841C0	/* 56.6.39：EP0 控制（复位值 0x00800080 已使能） */
#define USB1_ENDPTCTRL1			0x021841C4	/* 56.6.39：EP1 bulk 使能/halt */
#define USBNC_OTG1_CTRL			0x02184800	/* 56.5.1：非核心（过流/极性） */

/* ============ 0x0219_0000 USDHC1（RM 58 章，TF 卡）============ */
#define USDHC1_BASE			0x02190000
#define USDHC1_BLK_ATT			0x02190004	/* 58.8.2 Block Attributes */
#define USDHC1_CMD_ARG			0x02190008	/* 58.8.3 Command Argument */
#define USDHC1_CMD_XFR_TYP		0x0219000C	/* 58.8.4 Command Transfer Type */
#define USDHC1_CMD_RSP0			0x02190010	/* 58.8.5 Command Response0（RSP1-3 连续） */
#define USDHC1_DATA_BUFF_ACC_PORT	0x02190020	/* 58.8.7 数据缓冲口（PIO） */
#define USDHC1_PRES_STATE		0x02190024	/* 58.8.8 Present State（CMDINH/DLA/SDSTB） */
#define USDHC1_PROT_CTRL		0x02190028	/* 58.8.9 Protocol Control（DTW 总线宽度） */
#define USDHC1_SYS_CTRL			0x0219002C	/* 58.8.10 System Control（分频/复位/INITA） */
#define USDHC1_INT_STATUS		0x02190030	/* 58.8.11 Interrupt Status（W1C） */
#define USDHC1_INT_STATUS_EN		0x02190034	/* 58.8.12 Interrupt Status Enable */
#define USDHC1_INT_SIGNAL_EN		0x02190038	/* 58.8.13 Interrupt Signal Enable */
#define USDHC1_HOST_CTRL_CAP		0x02190040	/* 58.8.15 Host Controller Capabilities */
#define USDHC1_WTMK_LVL			0x02190044	/* 58.8.18 Watermark Level（PIO 水位） */
#define USDHC1_MIX_CTRL			0x02190048	/* 58.8.19 Mixer Control（DTDSEL/BCEN/MSBSEL） */
#define USDHC1_VEND_SPEC		0x021900C0	/* 58.8.27 Vendor Specific（HCKEN/IPGEN/CKEN） */

/* ============ 0x0219_4000 USDHC2（RM 58 章，eMMC）============ */
#define USDHC2_BASE			0x02194000
#define USDHC2_BLK_ATT			0x02194004
#define USDHC2_CMD_ARG			0x02194008
#define USDHC2_CMD_XFR_TYP		0x0219400C
#define USDHC2_CMD_RSP0			0x02194010
#define USDHC2_DATA_BUFF_ACC_PORT	0x02194020
#define USDHC2_PRES_STATE		0x02194024
#define USDHC2_PROT_CTRL		0x02194028
#define USDHC2_SYS_CTRL			0x0219402C
#define USDHC2_INT_STATUS		0x02194030
#define USDHC2_INT_STATUS_EN		0x02194034
#define USDHC2_INT_SIGNAL_EN		0x02194038
#define USDHC2_HOST_CTRL_CAP		0x02194040
#define USDHC2_WTMK_LVL			0x02194044
#define USDHC2_MIX_CTRL			0x02194048
#define USDHC2_VEND_SPEC		0x021940C0

/* ============ 0x021D_C000 SCTR（System Counter）============ */
#define SCTR_CNTCR			0x021DC000	/* 控制：ENABLE/HDBG/FCREQ */
#define SCTR_CNTFID0			0x021DC020	/* 频率 ID：CNTCR.FCREQ=01 时选中 */

/* ============ 0x021E_4000 WDOG3（RM 59 章，6ULL 第三只，16 位访问）============ */
#define WDOG3_WMCR			0x021E4008

#endif /* _JUSTICEBL_IMX6ULL_H */
