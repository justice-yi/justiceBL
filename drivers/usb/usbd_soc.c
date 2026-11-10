// SPDX-License-Identifier: MIT
/*
 * USB 设备控制器的 SoC 专属层（i.MX6ULL / ChipIdea IP）。
 *
 * 本文件集中所有 i.MX 具体地址与上电顺序，换 SoC 时只改这里。
 * 依据（IMX6ULL参考手册 Rev.1 11/2017，下称 RM）：
 *   - 控制器基址：RM 56.5 内存映射，USB OTG1 core = 0x0218_4000，核间距 512
 *   - PLL3：RM 18.7.2 CCM_ANALOG_PLL_USB1 @ 0x020C_8010（USB1 PLL 480MHz）
 *   - 门控：RM 18.6.29 CCM_CCGR6 @ 0x020C_4080，CG0 = usboh3_clk
 *   - PHY：RM 57.3，USBPHY1 @ 0x020C_9000（PWD 复位值 001E_1C00=全断电；
 *     CTRL 复位值 C020_0000，bit31 SFTRST / bit30 CLKGATE 置位）
 *   - 3P0 LDO：RM 39.4/39.6.2，REG_3P0 @ 0x020C_8120（复位态=旁路+限流+
 *     自保模式，RM 要求软件切正常稳压——ROM/U-Boot/内核都漏了这步）
 *   - 过流/充电检测：RM 56.5.1 / 57.2.7
 */

#include <types.h>
#include <imx6ull.h>
#include "usbd_soc.h"

/* ---- 地址（RM 出处见文件头） ---- */
#define USBOH3_OTG1_BASE	0x02184000
#define CCM_ANALOG_BASE		0x020C8000	/* RM：ANATOP 块基址（PLL_SET/CLR 所在） */
#define PLL_USB1		0x020C8010	/* RM 18.7.2：CTRL @ base+0x10 */
#define PLL_USB1_SET_OFF	0x14		/* RM 18.7.2：base+0x14 */
#define PLL_USB1_CLR_OFF	0x18		/* RM 18.7.2：base+0x18 */
#define CCM_CCGR6_OFF		0x80		/* RM 18.6.29：CCM base + 0x80 */
#define USBPHY1_PWD		0x020C9000	/* RM 57.3：PWD @ PHY base+0x00 */
#define USBPHY1_CTRL_SET	0x020C9034	/* CTRL_SET */
#define USBPHY1_CTRL_CLR	0x020C9038	/* CTRL_CLR */
#define PLL_USB2_CLR		0x020C8038	/* RM：PLL_USB2 @ base+0x20，CLR=+0x38 */
#define PMU_REG_3P0_SET		0x020C8124	/* RM 39.6.2 */
#define PMU_REG_3P0_CLR		0x020C8128
#define USBNC_OTG1_BASE		0x02184800	/* RM 56.5.1 */
#define USB1_CHRG_DETECT	0x020C81B0	/* RM 57.4.2 */

/* PLL_USB1 位（RM 18.7.2 + U-Boot usb_power_config_mx6 同款集合） */
#define PLL_LOCK		BIT(31)
#define PLL_BYPASS		BIT(16)
#define PLL_ENABLE		BIT(13)		/* 480MHz 输出到 CCM（给别的模块） */
#define PLL_POWER		BIT(12)
#define PLL_EN_USB_CLKS		BIT(6)		/* ★480MHz 输出到 USB PHY——没它 PHY 无时钟 */

/* REG_3P0 位（RM 39.6.2） */
#define EN_LINREG		BIT(0)		/* 线性稳压输出使能——复位态 0=旁路+自保 */
#define EN_BO			BIT(1)		/* brownout 检测使能（复位态 0） */
#define EN_ILIMIT		BIT(2)		/* 限流（复位态 1=开；RM 39.4：负载前应关） */

/* USBPHY CTRL 位（RM 57.3.4） */
#define PHY_SFTRST		BIT(31)
#define PHY_CLKGATE		BIT(30)
#define PHY_ENUTMILEVEL2	BIT(14)		/* FS/LS 设备使能（U-Boot 同款） */
#define PHY_ENUTMILEVEL3	BIT(15)

/* CCGR6 CG0 = usboh3_clk（RM 18.6.29），两 bit 全开 */
#define CCGR_CG0_MASK		(0x3u << 0)

/* USBNC USB_OTG1_CTRL 位（RM 56.5.1） */
#define USBNC_OVER_CUR_DIS	BIT(7)		/* 禁用过流检测（板上无 OC 采样引脚） */
#define USBNC_OVER_CUR_POL	BIT(8)		/* 过流极性（U-Boot 同款置位） */

/* USB1 充电检测器（RM 57.2.7/57.4.2）：EN_B|CHK_CHRG_B 全关，
 * 否则充电检测电路接在 DP 上，DP 信号质量差、主机可能不识别 */
#define CHRG_EN_B		BIT(20)
#define CHRG_CHK_CHRG_B		BIT(19)

/* ~10us 级粗延迟（SFTRST/CLKGATE 时序用；CPU 396MHz 下 4000 圈绰绰有余） */
static void delay_10us(void)
{
	volatile u32 n = 4000;

	while (n--)
		;
}

u32 usbd_soc_core_base(void)
{
	return USBOH3_OTG1_BASE;
}

u32 usbd_soc_rd32(u32 off)
{
	return readl(USBOH3_OTG1_BASE + off);
}

void usbd_soc_wr32(u32 off, u32 val)
{
	writel(val, USBOH3_OTG1_BASE + off);
}

/*
 * 时钟：PLL3(USB1 PLL) 的 480MHz 输出经 bit6(EN_USB_CLKS) 直达 USBPHY 的
 * UTMI 时钟——bit13(ENABLE) 只是把时钟送进 CCM 给别的模块，两者都要置位。
 * 写法用 SET/CLR 寄存器对（与 U-Boot usb_power_config_mx6 一致），
 * 避免对运行中的 PLL 做 read-modify-write。
 *
 * 顺带断掉 Linux 遗留的 PLL_USB2 模拟芯：Linux 曾拉起 usbotg2/usbphy2/
 * PLL_USB2，WDOG 复位不清 ANATOP——遗留负载在下载会话里白吃
 * VDD_HIGH/VDD_USB 轨（U-Boot fastboot 的 fresh 链没有这笔负载）。
 */
int usbd_soc_clock_init(void)
{
	u32 to = 1000000;

	writel(PLL_BYPASS, CCM_ANALOG_BASE + PLL_USB1_CLR_OFF);
	writel(PLL_POWER | PLL_ENABLE | PLL_EN_USB_CLKS,
	       CCM_ANALOG_BASE + PLL_USB1_SET_OFF);

	/* 等 PLL 锁定（LOCK bit31），超时判失败防止死等 */
	while (!(readl(PLL_USB1) & PLL_LOCK)) {
		if (--to == 0)
			return -1;
	}

	writel(readl(MX6ULL_CCM_BASE + CCM_CCGR6_OFF) | CCGR_CG0_MASK,
	       MX6ULL_CCM_BASE + CCM_CCGR6_OFF);

	writel(PLL_POWER, PLL_USB2_CLR);	/* 断遗留 PLL_USB2 */
	return 0;
}

/*
 * PHY 上电序列（U-Boot usb_phy_enable 同款顺序）：
 *   1. 3P0 LDO 切正常稳压（RM 39.4 要求的软件步骤，顺序：开稳压+brownout
 *      监视 -> 稳压建立 -> 关限流；复位态是旁路+自保，带不动 PHY 满负荷）
 *   2. SFTRST 置位 -> 等 -> CLKGATE|SFTRST 一起清（各留 10us 余量）
 *   3. PWD 清零——解除 TX/RX/偏置全部断电位（复位值 001E_1C00 全断电）
 *   4. ENUTMILEVEL2|3 置位——FS/LS 设备使能
 *   5. 充电检测器全关（RM 57.2.7：不关则 DP 信号质量差）
 *   6. 过流：极性置位 + 检测关闭（板上无 OC 采样引脚）
 */
void usbd_soc_phy_init(void)
{
	writel(EN_LINREG | EN_BO, PMU_REG_3P0_SET);
	delay_10us();
	delay_10us();
	writel(EN_ILIMIT, PMU_REG_3P0_CLR);

	writel(PHY_SFTRST, USBPHY1_CTRL_SET);
	delay_10us();
	writel(PHY_CLKGATE | PHY_SFTRST, USBPHY1_CTRL_CLR);
	delay_10us();

	writel(0, USBPHY1_PWD);

	writel(PHY_ENUTMILEVEL2 | PHY_ENUTMILEVEL3, USBPHY1_CTRL_SET);

	writel(CHRG_EN_B | CHRG_CHK_CHRG_B, USB1_CHRG_DETECT);

	writel(readl(USBNC_OTG1_BASE) | USBNC_OVER_CUR_DIS | USBNC_OVER_CUR_POL,
	       USBNC_OTG1_BASE);
}
