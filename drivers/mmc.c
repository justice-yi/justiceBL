// SPDX-License-Identifier: MIT
/*
 * justiceBL SD/MMC 子系统 —— USDHC 主机驱动 + SD/eMMC 卡识别。
 *
 * 依据（按项目规矩先读手册与参考后下手）：
 *   - RM uSDHC 章节（58 章）：两段分频公式（图 58-14：card_clk =
 *     Base/(DVS+1)/SDCLKFS_DIV，后一档 2 的幂）、时钟切换先停 CKEN
 *     等 SDSTB（"Before changing clock divisor value...make sure the
 *     SDSTB bit is high"）；寄存器布局与 MCIMX6Y2.h 的 USDHC_Type 一致
 *   - U-Boot drivers/mmc/fsl_esdhc_imx.c + include/fsl_esdhc_imx.h：补 RM
 *     两处空白——VENDORSPEC 位表（RM 只提 VSELECT，CKEN=bit14 等无位表）、
 *     DTW 编码（RM 表格为图无法文本提取；DTW[2:1] 01=4bit）；另 XFERTYP/
 *     MIX_CTRL 用法、CMD6/ACMD6 切换时序（规范层，RM 不覆盖）
 *   - 第三方参考 imx6ull_NoosProgramProject/14_TF卡编程（CMD 标志位编码交叉验证）
 *   - 引脚/门控：kernel dts imx6ull-alientek-emmc.dts:633,666（pad 值，
 *     内核跑 50MHz+ 同值）、imx6ul-pinfunc.h（偏移）、clk-imx6ul.c:446
 *
 * 保守取舍：识别后切 4 位 + High Speed（49.5MHz，v0.12 前为 1 位 400kHz，
 * 600KB 要 12.8s）；仍逐块 CMD17 PIO——CMD18 多块读 + 省命令开销是后续课题。
 * 所有等待均有界——本项目铁律：任何路径都不许死循环。
 */

#include <types.h>
#include <imx6ull.h>
#include <timer.h>
#include <mmc.h>
#include <math.h>
#include <stddef.h>
#include <printf.h>
#include <blocksrc.h>

/* ---- USDHC 寄存器：地址在 imx6ull.h 词典（RM 58 章 USDHC1_/USDHC2_
 * 确切实例）。双实例同构，此处用同构地址表按卡选用，字段名 = RM 寄存
 * 名小写 ---- */
struct usdhc_map {
	u32 blk_att;
	u32 cmd_arg;
	u32 cmd_xfr_typ;
	u32 cmd_rsp0;		/* RSP1-3 = cmd_rsp0 + i*4（连续） */
	u32 data_buff_acc_port;
	u32 pres_state;
	u32 prot_ctrl;
	u32 sys_ctrl;
	u32 int_status;
	u32 int_status_en;
	u32 int_signal_en;
	u32 wtmk_lvl;
	u32 mix_ctrl;
	u32 adma_sys_addr;
	u32 vend_spec;
};

static const struct usdhc_map usdhc1_map = {
	.blk_att		= USDHC1_BLK_ATT,
	.cmd_arg		= USDHC1_CMD_ARG,
	.cmd_xfr_typ		= USDHC1_CMD_XFR_TYP,
	.cmd_rsp0		= USDHC1_CMD_RSP0,
	.data_buff_acc_port	= USDHC1_DATA_BUFF_ACC_PORT,
	.pres_state		= USDHC1_PRES_STATE,
	.prot_ctrl		= USDHC1_PROT_CTRL,
	.sys_ctrl		= USDHC1_SYS_CTRL,
	.int_status		= USDHC1_INT_STATUS,
	.int_status_en		= USDHC1_INT_STATUS_EN,
	.int_signal_en		= USDHC1_INT_SIGNAL_EN,
	.wtmk_lvl		= USDHC1_WTMK_LVL,
	.mix_ctrl		= USDHC1_MIX_CTRL,
	.adma_sys_addr		= USDHC1_ADMA_SYS_ADDR,
	.vend_spec		= USDHC1_VEND_SPEC,
};

static const struct usdhc_map usdhc2_map = {
	.blk_att		= USDHC2_BLK_ATT,
	.cmd_arg		= USDHC2_CMD_ARG,
	.cmd_xfr_typ		= USDHC2_CMD_XFR_TYP,
	.cmd_rsp0		= USDHC2_CMD_RSP0,
	.data_buff_acc_port	= USDHC2_DATA_BUFF_ACC_PORT,
	.pres_state		= USDHC2_PRES_STATE,
	.prot_ctrl		= USDHC2_PROT_CTRL,
	.sys_ctrl		= USDHC2_SYS_CTRL,
	.int_status		= USDHC2_INT_STATUS,
	.int_status_en		= USDHC2_INT_STATUS_EN,
	.int_signal_en		= USDHC2_INT_SIGNAL_EN,
	.wtmk_lvl		= USDHC2_WTMK_LVL,
	.mix_ctrl		= USDHC2_MIX_CTRL,
	.adma_sys_addr		= USDHC2_ADMA_SYS_ADDR,
	.vend_spec		= USDHC2_VEND_SPEC,
};

/* ---- ADMA2 描述符（RM 58.4.1.4 图 58-11：8 字节一行，32 位地址版）----
 * 布局（小端）：[15:0] 属性 | [31:16] 长度（字节，≤65535）| [63:32] 地址
 * 属性：bit0=Valid（0 会触发 ADMA 错误停机）bit1=End（末条，完成置 TC）
 *      bit5=Tran（传输型描述符，一条即含数据地址与长度） */
struct adma2_desc {
	u16 attr;
	u16 len;
	u32 addr;
} __attribute__((packed));
#define ADMA2_ATTR_VALID	BIT(0)
#define ADMA2_ATTR_END		BIT(1)
#define ADMA2_ATTR_TRAN		BIT(5)

/* 描述符表：单条 16KB 描述符（.bss OCRAM——USDHC DMA 主口可访问，
 * USB dQH 同例）。8 字节对齐（RM：地址字段字对齐，表按行尺寸对齐） */
static struct adma2_desc adma_desc[2] __attribute__((aligned(8)));

/* 按实例基址取同构地址表 */
static const struct usdhc_map *usdhc_map_get(u32 base)
{
	return base == USDHC1_BASE ? &usdhc1_map : &usdhc2_map;
}

/* ADMA2 描述符表组装：单条 Tran+End 描述符指向数据缓冲。
 * bytes ≤ 16384 < 65535（16 位长度字段上限），单条即完整传输 */
static void mmc_adma2_setup(const struct usdhc_map *m, const void *buf,
			    u32 bytes)
{
	adma_desc[0].attr = ADMA2_ATTR_VALID | ADMA2_ATTR_END |
			    ADMA2_ATTR_TRAN;
	adma_desc[0].len = (u16)bytes;
	adma_desc[0].addr = (u32)buf;
	writel((u32)adma_desc, m->adma_sys_addr);	/* 表基址 → ADS_ADDR */
}

/* INT_STATUS_EN：bit0-5 正常事件(CC/TC/BGE/DINT/BWR/BRR)，bit16-24 错误
 * (CTOE/CCE/CEBE/CIE/DTOE/DCE/DEBE/AC12E)。★AC12E(bit24) 此前漏开——
 * auto CMD12 出错时 CNIBAC12E 静默不产生中断，AC12E 是唯一可见线索
 * （agent 对拍 2026-11-20） */
#define IRQEN_NORMAL		0x0000003FU
#define IRQEN_ERRORS		0x01FF0000U

/* ---- 位定义（值与 U-Boot fsl_esdhc_imx.h 一致）---- */
#define IRQSTAT_CC		BIT(0)
#define IRQSTAT_TC		BIT(1)
#define IRQSTAT_BWTEN		BIT(4)
#define IRQSTAT_BRR		BIT(5)
#define IRQSTAT_CTOE		BIT(16)
#define IRQSTAT_CCE		BIT(17)
#define IRQSTAT_DTOE		BIT(20)
/* PSTATE（RM 58.8.6）：发命令前须等三位全清（U-Boot fsl_esdhc.c:358-363
 * 同款三连等）。写块 TC 后卡拉 DAT0 编程忙期间 DLA 仍置位，不等就发
 * 命令会被控制器压住——CC 永不到来、也等不到超时错误（2026-11-13 实证） */
#define PSTATE_CIHB		BIT(0)		/* Command Inhibit (CMD) */
#define PSTATE_CIDHB		BIT(1)		/* Command Inhibit (DAT) */
#define PSTATE_DLA		BIT(2)		/* Data Line Active */
#define PSTATE_DAT0_LVL		BIT(24)		/* DLSL[0]：DAT0 电平（低=卡忙）*/
/* PIO 数据口就绪轮询用 PSTATE 电平位（RM 58.8.10："used for non-DMA
 * transfers"），不用 INT_STATUS 的 BWR/BRR 边沿事件——上笔排空后
 * 电平已为 1，无新上升沿，边沿位永不置（agent 对拍 2026-11-20：
 * U-Boot fsl_esdhc_imx.c:264 / 内核 sdhci.c:590 均轮询电平） */
#define PSTATE_BWEN		BIT(10)		/* Buffer Write Enable（电平） */
#define PSTATE_BREN		BIT(11)		/* Buffer Read Enable（电平） */
#define SYSCTL_RSTA		BIT(24)
#define SYSCTL_RSTC		BIT(25)
#define SYSCTL_RSTD		BIT(26)
#define SYSCTL_INITA		BIT(27)
#define PRSSTAT_SDSTB		BIT(3)		/* RM：改分频前必须等到它为 1 */
#define VENDORSPEC_CKEN		0x00004000U	/* RM 未给位表，值自 U-Boot 头 */
#define PROT_CTRL_DTW_MASK	0x00000006U	/* DTW[2:1]（编码同上 U-Boot）*/
#define PROT_CTRL_DTW_4BIT	0x00000002U
#define PROT_CTRL_DTW_8BIT	0x00000004U	/* 8-bit（DTW=10b，RM 58.8.9）*/
#define VENDORSPEC_INIT		0x20007809U
#define VENDORSPEC_HCKEN	0x00001000U
#define VENDORSPEC_IPGEN	0x00000800U
#define XFERTYP_RSPTYP_136	BIT(16)
#define XFERTYP_RSPTYP_48	(2 << 16)
#define XFERTYP_RSPTYP_48_BUSY	(3 << 16)
#define XFERTYP_CCCEN		BIT(19)
#define XFERTYP_CICEN		BIT(20)
#define XFERTYP_DPSEL		BIT(21)
#define XFERTYP_CMDTYP_ABORT	(3 << 22)	/* CMD12 STOP_TRANSMISSION 用 */
#define MIX_CTRL_BCEN		BIT(1)
#define MIX_CTRL_DMAEN		BIT(0)		/* DMA 使能（模式由 PROT_CTRL.DMASEL 选） */
#define MIX_CTRL_DTDSEL_READ	BIT(4)
#define MIX_CTRL_MSBSEL		BIT(5)		/* 多块选择（CMD18/25 用） */
/* PROT_CTRL.DMASEL [9:8]（RM 58.8.9）：DMA 使能时选择 DMA 类型——
 * 00=无/SDMA 01=ADMA1 10=ADMA2 11=保留。i.MX 上标准 SDHCI 的 DMA
 * 选择位 [4:5] 移位到 [9:8]（kernel sdhci-esdhc-imx.c:804 同款移位） */
#define PROT_CTRL_DMASEL_MASK	(3U << 8)
#define PROT_CTRL_DMASEL_ADMA2	(2U << 8)

#define USDHC_FIFO_WORDS	128		/* RM 58.4.1.2：缓冲 128 字深 */

#define MMC_BLOCK_SIZE		512

/* ---- CID/CSD 位段取值（kernel drivers/mmc/core/mmc.h 的 UNSTUFF_BITS 同义，
 * 数组方向适配：本驱动 raw[0]=最高 32 位，内核 resp[3]=最高） ---- */
#define UNSTUFF_BITS(raw, from, size) ({					\
	const int __size = (size);						\
	const u32 __mask = (__size < 32 ? 1u << __size : 0u) - 1u;		\
	const int __off = (from) / 32;						\
	const int __shft = (from) % 32;					\
	u32 __res = (raw)[__off] >> __shft;					\
	if (__size + __shft > 32)						\
		__res |= (raw)[__off + 1] << ((32 - __shft) % 32);		\
	__res & __mask;							\
})

/* R2 响应（CID）四字捕获 + 去移位（r2_deshift 定义在识别层，前置声明） */
static void r2_deshift(u32 raw[4]);
static void r2_capture(struct mmc_card *card, const struct usdhc_map *m)
{
	for (int i = 0; i < 4; i++)
		card->cid_raw[i] = readl(m->cmd_rsp0 + i * 4);
	r2_deshift(card->cid_raw);
}
#define WAIT_LOOPS		2000000U	/* 有界等待上限，约百毫秒级 */

/* ---- 板级引脚（IOMUXC 寄存器地址在 imx6ull.h 词典，RM 名；
 * mux 模式/pad 值源自 kernel dts imx6ul-pinfunc.h + alientek dts）---- */
struct pin_cfg {
	u32 mux_reg;		/* IOMUXC_SW_MUX_CTL_PAD_xxx（词典绝对地址） */
	u32 pad_reg;		/* IOMUXC_SW_PAD_CTL_PAD_xxx */
	u8 mux_mode;
	u32 pad_val;
};

static const struct pin_cfg sd1_pins[] = {	/* TF 卡，USDHC1 */
	{ IOMUXC_SW_MUX_CTL_PAD_SD1_CLK, IOMUXC_SW_PAD_CTL_PAD_SD1_CLK,
	  0, 0x10071 },
	{ IOMUXC_SW_MUX_CTL_PAD_SD1_CMD, IOMUXC_SW_PAD_CTL_PAD_SD1_CMD,
	  0, 0x17059 },
	{ IOMUXC_SW_MUX_CTL_PAD_SD1_DATA0, IOMUXC_SW_PAD_CTL_PAD_SD1_DATA0,
	  0, 0x17059 },
	{ IOMUXC_SW_MUX_CTL_PAD_SD1_DATA1, IOMUXC_SW_PAD_CTL_PAD_SD1_DATA1,
	  0, 0x17059 },
	{ IOMUXC_SW_MUX_CTL_PAD_SD1_DATA2, IOMUXC_SW_PAD_CTL_PAD_SD1_DATA2,
	  0, 0x17059 },
	{ IOMUXC_SW_MUX_CTL_PAD_SD1_DATA3, IOMUXC_SW_PAD_CTL_PAD_SD1_DATA3,
	  0, 0x17059 },
};

static const struct pin_cfg emmc_pins[] = {	/* eMMC，USDHC2（NAND 复用脚）*/
	{ IOMUXC_SW_MUX_CTL_PAD_NAND_RE_B, IOMUXC_SW_PAD_CTL_PAD_NAND_RE_B,
	  1, 0x10069 },					/* ALT1 = USDHC2_CLK */
	{ IOMUXC_SW_MUX_CTL_PAD_NAND_WE_B, IOMUXC_SW_PAD_CTL_PAD_NAND_WE_B,
	  1, 0x17059 },					/* ALT1 = USDHC2_CMD */
	{ IOMUXC_SW_MUX_CTL_PAD_NAND_DATA00, IOMUXC_SW_PAD_CTL_PAD_NAND_DATA00,
	  1, 0x17059 },
	{ IOMUXC_SW_MUX_CTL_PAD_NAND_DATA01, IOMUXC_SW_PAD_CTL_PAD_NAND_DATA01,
	  1, 0x17059 },
	{ IOMUXC_SW_MUX_CTL_PAD_NAND_DATA02, IOMUXC_SW_PAD_CTL_PAD_NAND_DATA02,
	  1, 0x17059 },
	{ IOMUXC_SW_MUX_CTL_PAD_NAND_DATA03, IOMUXC_SW_PAD_CTL_PAD_NAND_DATA03,
	  1, 0x17059 },
	/* 8-bit 总线扩展腿（DATA4-7，2026-11-21 ②）：dts 8bit 组同款 */
	{ IOMUXC_SW_MUX_CTL_PAD_NAND_DATA04, IOMUXC_SW_PAD_CTL_PAD_NAND_DATA04,
	  1, 0x17059 },
	{ IOMUXC_SW_MUX_CTL_PAD_NAND_DATA05, IOMUXC_SW_PAD_CTL_PAD_NAND_DATA05,
	  1, 0x17059 },
	{ IOMUXC_SW_MUX_CTL_PAD_NAND_DATA06, IOMUXC_SW_PAD_CTL_PAD_NAND_DATA06,
	  1, 0x17059 },
	{ IOMUXC_SW_MUX_CTL_PAD_NAND_DATA07, IOMUXC_SW_PAD_CTL_PAD_NAND_DATA07,
	  1, 0x17059 },
};

/* USDHC2 daisy 输入选择（SELECT_INPUT）：MUX 选输出、daisy 选输入采样路径。
 * 不写 = 复位值 00 指向 CSI/LCD pad，CMD 线响应永远采不到 → CMD1 挂死
 * （2026-11-19 agent 对拍 U-Boot pinctrl 定案）。USDHC1 专用 pad 无此项。
 * 值 = NAND pad 在各自 daisy 寄存器的编码（DATA2 = 1，其余 = 2） */
static void emmc_daisy_setup(void)
{
	writel(2, IOMUXC_USDHC2_CLK_SELECT_INPUT);
	writel(2, IOMUXC_USDHC2_CMD_SELECT_INPUT);
	writel(2, IOMUXC_USDHC2_DATA0_SELECT_INPUT);
	writel(2, IOMUXC_USDHC2_DATA1_SELECT_INPUT);
	writel(1, IOMUXC_USDHC2_DATA2_SELECT_INPUT);
	writel(2, IOMUXC_USDHC2_DATA3_SELECT_INPUT);
	/* DATA4-7（8-bit 总线腿，②）：pinfunc 实测 NAND 路线值全 = 1 */
	writel(1, IOMUXC_USDHC2_DATA4_SELECT_INPUT);
	writel(1, IOMUXC_USDHC2_DATA5_SELECT_INPUT);
	writel(1, IOMUXC_USDHC2_DATA6_SELECT_INPUT);
	writel(1, IOMUXC_USDHC2_DATA7_SELECT_INPUT);
}

static void pins_setup(const struct pin_cfg *pins, u32 n)
{
	for (u32 i = 0; i < n; i++) {
		writel(pins[i].mux_mode, pins[i].mux_reg);
		writel(pins[i].pad_val, pins[i].pad_reg);
	}
}

/* ---- 主机层 ---- */

/** 等寄存器某些位变 0/变 1，有界；1=等到，0=超时 */
static int usdhc_wait_clear(u32 reg, u32 mask)
{
	u32 t = WAIT_LOOPS;

	while (readl(reg) & mask) {
		if (!--t) {
			return 0;
		}
	}
	return 1;
}

static int usdhc_wait_set(u32 reg, u32 mask)
{
	u32 t = WAIT_LOOPS;

	while (!(readl(reg) & mask)) {
		if (!--t) {
			return 0;
		}
	}
	return 1;
}

/* 无 libgcc 的除法已抽到 lib/math.c（udiv32/udiv64_mod），此处引用 */

/* 基准时钟运行时计算，算法对齐 kernel clk-imx6ul.c:39,248-249,299-300：
 *   usdhc root = PLL2 PFD（CSCMR1 bit16/17 mux：0=pll2_pfd2 396M, 1=pll2_pfd0 352M）
 *              ÷ podf（CSCDR1：usdhc1 bit11、usdhc2 bit16，3 位 ÷1~8）
 * 注意：HOST_CAP[15:8] 在 i.MX 上是 retuning 能力位而非 SDHCI 标准的
 * base-clock 字段，不可用于读频率（v0.5.2 曾误读）。 */
static u32 usdhc_base_clock_mhz(u32 base)
{
	u32 is_usdhc1 = (base == USDHC1_BASE);
	u32 sel = (readl(CCM_CSCMR1) >>
		   (is_usdhc1 ? 16 : 17)) & 1;
	u32 podf = ((readl(CCM_CSCDR1) >>
		     (is_usdhc1 ? 11 : 16)) & 7) + 1;

	return udiv32(sel ? 352 : 396, podf);
}

/**
 * usdhc_set_clock() - SD 时钟切到 <=target 的最大可达档；返回实际频率
 *
 * 两段分频（RM 图 58-14）：card_clk = Base / (DVS+1) / SDCLKFS_DIV，
 * SDCLKFS_DIV 只有 2 的幂档（1,2,...,256）——比 v0.11 前的任意整数 fs
 * 更保守，也贴合 RM 文字"by (1*), 2, 4, ..., 256"。字段编码 = 档位/2。
 * 切换时序（RM SDSTB 描述 + U-Boot set_sysctl 同款实证）：
 * 停卡钟（VENDORSPEC.CKEN）→ 写分频 → 等 SDSTB=1 → 复开卡钟。
 *
 * Return: 实际 Hz（<0 = SDSTB 等待超时）
 */
static int usdhc_set_clock(u32 base, u32 target_hz)
{
	const struct usdhc_map *m = usdhc_map_get(base);
	u32 base_hz = usdhc_base_clock_mhz(base) * 1000000U;
	u32 dvs, fs, fsdiv;

	/* 从最小分频向大搜：首个 <=target 的组合即"不超目标的最大频率"。
	 * （v0.5 曾从最大分频端搜索，恒命中 Base/8192≈24kHz，比最优慢 16 倍）*/
	for (dvs = 0; dvs <= 15; dvs++) {
		for (fsdiv = 1; fsdiv <= 256; fsdiv <<= 1) {
			if (udiv32(base_hz, (dvs + 1) * fsdiv) <= target_hz) {
				goto found;
			}
		}
	}
	dvs = 15;			/* 兜底（Base 低到无解时取最小频率）*/
	fsdiv = 256;
found:
	fs = fsdiv >> 1;		/* 字段值 = 档位/2，0 即 ÷1 */
	writel(readl(m->vend_spec) & ~VENDORSPEC_CKEN,
	       m->vend_spec);
	writel((readl(m->sys_ctrl) & ~0x0000FFF0U) |
	       (fs << 8) | (dvs << 4), m->sys_ctrl);
	if (!usdhc_wait_set(m->pres_state, PRSSTAT_SDSTB)) {
		return -1;
	}
	writel(readl(m->vend_spec) | VENDORSPEC_CKEN,
	       m->vend_spec);
	return (int)udiv32(base_hz, (dvs + 1) * fsdiv);
}

/** 主机复位 + 通用初始化（U-Boot fsl_esdhc_init 同套路）*/
static int usdhc_hw_init(u32 base)
{
	const struct usdhc_map *m = usdhc_map_get(base);

	writel(readl(m->sys_ctrl) |
	       SYSCTL_RSTA | SYSCTL_RSTC | SYSCTL_RSTD,
	       m->sys_ctrl);
	if (!usdhc_wait_clear(m->sys_ctrl,
			      SYSCTL_RSTA | SYSCTL_RSTC | SYSCTL_RSTD)) {
		return -1;
	}

	writel(0, m->mix_ctrl);
	writel(VENDORSPEC_INIT, m->vend_spec);
	writel(readl(m->vend_spec) |
	       VENDORSPEC_HCKEN | VENDORSPEC_IPGEN,
	       m->vend_spec);

	/* 发 80 个初始化时钟（INITA 自清）*/
	writel(readl(m->sys_ctrl) | SYSCTL_INITA,
	       m->sys_ctrl);
	if (!usdhc_wait_clear(m->sys_ctrl, SYSCTL_INITA)) {
		return -1;
	}

	/* 状态使能开正常+错误事件；信号使能显式清 0（不依赖复位默认）——纯轮询 */
	writel(IRQEN_NORMAL | IRQEN_ERRORS, m->int_status_en);
	writel(0, m->int_signal_en);
	writel(0xFFFFFFFF, m->int_status);/* 写 1 清中断状态历史 */
	/* DMASEL=10b 选 ADMA2（RM 58.8.9；RSTA 后回到 0，须每次 hw_init 重设） */
	writel((readl(m->prot_ctrl) & ~PROT_CTRL_DMASEL_MASK) |
	       PROT_CTRL_DMASEL_ADMA2, m->prot_ctrl);
	/* DTOCV=0xE（数据超时 2^27 SD 周期 ≈ 2.7s@49.5MHz）：复位值 0
	 * 只有 ≈165µs——eMMC 编程 busy 轻松超它，短超时在 busy 期误报
	 * DTOE 中止传输。U-Boot fsl_esdhc_imx.c:1040 同款（agent 排查
	 * 2026-11-20） */
	writel((readl(m->sys_ctrl) & ~(0xFU << 16)) | (0xEU << 16),
	       m->sys_ctrl);
	/* WML 水位 128/128 + 突发长度 31/31（复位 16 与 8/16）：
	 * 水位 = PIO 分段吞吐前提；★突发长度 = ADMA AHB 取数效率关键★
	 * （8-bit 总线翻倍后 ADMA 短突发取数成为新短板，拉满突发提升
	 * DDR 取数效率。RM 58.8.18：RD_BRST[12:8]/WR_BRST[28:24]，1-31） */
	writel((readl(m->wtmk_lvl) & ~0x1FFF1FFFU) | 0x1F801F80U,
	       m->wtmk_lvl);
	writel((1 << 16) | MMC_BLOCK_SIZE, m->blk_att);/* 块长 512，count 占位 */
	writel(0x00000008, m->prot_ctrl);	/* 1 位总线 + 大端数据口 */
	if (usdhc_set_clock(base, 400000) < 0) {
		return -2;
	}
	return 0;
}

/**
 * usdhc_send_cmd() - 发一条命令并（可选）收 48 位响应
 * @xfr_hi: XFERTYP 高 16 位（响应类型/校验/DPSEL）
 * @mix_lo: MIX_CTRL 数据 mode 位（无数据命令传 0）
 * @rsp0:   非 NULL 时回填 RSP0
 *
 * Return: 0 成功；-1 超时；-2 命令错误（CTOE/CCE）
 */
static int usdhc_send_cmd(u32 base, u8 idx, u32 arg, u32 xfr_hi, u32 mix_lo,
			  u32 *rsp0)
{
	const struct usdhc_map *m = usdhc_map_get(base);

	if (!usdhc_wait_clear(m->pres_state,
			      PSTATE_CIHB | PSTATE_CIDHB | PSTATE_DLA)) {
		return -10;	/* 总线未空闲（诊断区分码）：控制器级，
				 * 与"命令发出但无响应"的 -1 不同源 */
	}
	writel(0xFFFFFFFF, m->int_status);
	writel(arg, m->cmd_arg);
	writel((readl(m->mix_ctrl) & 0xFFFFFF80U) | mix_lo,
	       m->mix_ctrl);
	writel(((u32)idx << 24) | xfr_hi, m->cmd_xfr_typ);

	if (!usdhc_wait_set(m->int_status, IRQSTAT_CC)) {
		return -1;	/* 命令已发，CC 不到：无响应命令也该到 CC，
				 * 不到 = 命令线时钟/控制器异常 */
	}
	if (readl(m->int_status) &
	    (IRQSTAT_CTOE | IRQSTAT_CCE)) {
		return -2;
	}
	if (rsp0) {
		*rsp0 = readl(m->cmd_rsp0);
	}
	return 0;
}

/** 从数据口收 nwords 个字到 buf（PIO，BRR 轮询；块长/块数由调用方设）*/
/* PIO 收流：分段等 BRR 拉 FIFO（128 字深，WML=128 时每段满窗），全部
 * 收完等 TC。单块（CMD17/CMD6/EXT_CSD）与多块（CMD18）通用——块数由
 * BLK_ATT 决定，本函数只管把 nwords 收完。
 * 数据端口是大端语义（板上实测 v0.5 MISMATCH → swab32 全匹配确诊），
 * ARM 小端内存需在驱动边界归一化。 */
static int usdhc_read_data_port(u32 base, u32 *w, u32 nwords)
{
	const struct usdhc_map *m = usdhc_map_get(base);
	u32 *p = w;

	while (nwords) {
		u32 take = nwords;

		if (!usdhc_wait_set(m->pres_state, PSTATE_BREN)) {
			printf("mmc: rport BREN timeout, pstate=%08x\n",
			       readl(m->pres_state));
			return -1;
		}
		if (take > USDHC_FIFO_WORDS)
			take = USDHC_FIFO_WORDS;
		for (u32 i = 0; i < take; i++) {
			*p++ = __builtin_bswap32(readl(m->data_buff_acc_port));
		}
		nwords -= take;
	}
	if (!usdhc_wait_set(m->int_status, IRQSTAT_TC)) {
		printf("mmc: rport TC timeout, sts=%08x\n",
		       readl(m->int_status));
		return -2;
	}
	if (readl(m->int_status) &
	    (IRQSTAT_DTOE | IRQSTAT_CCE)) {
		printf("mmc: rport data err, sts=%08x\n",
		       readl(m->int_status));
		return -2;
	}
	writel(IRQSTAT_TC | IRQSTAT_BRR, m->int_status);
	return 0;
}

/* PIO 写函数已随 ADMA2 改造退役（2026-11-21）：写路径全部走 ADMA2
 * 硬件搬运；读方向 PIO 仍保留（EXT_CSD/SCR/CMD6 状态读） */

/* 数据命令失败后的错误恢复（U-Boot fsl_esdhc_imx.c:568-586 同款）：
 * RSTC 清命令线状态 → RSTD 清数据线/缓冲 → CMD12 ABORT 停卡。
 * RM 58.4.1.1 明文：出错时 uSDHC 不发 auto CMD12，须由驱动收尾，
 * 否则卡留在 rcv 态、控制器进 CNIBAC12E 静默吞命令态 */
static void mmc_recover(struct mmc_card *card)
{
	u32 base = card->base;
	const struct usdhc_map *m = usdhc_map_get(base);

	writel(readl(m->sys_ctrl) | SYSCTL_RSTC, m->sys_ctrl);
	while (readl(m->sys_ctrl) & SYSCTL_RSTC)
		;
	writel(readl(m->sys_ctrl) | SYSCTL_RSTD, m->sys_ctrl);
	while (readl(m->sys_ctrl) & SYSCTL_RSTD)
		;
	usdhc_send_cmd(base, 12, 0,
		       XFERTYP_RSPTYP_48_BUSY | XFERTYP_CMDTYP_ABORT,
		       0, NULL);
}

/**
 * 等卡写编程完成：CMD13 SEND_STATUS 轮询 R1.READY_FOR_DATA(bit8)。
 * CMD24 数据 TC 只代表主机搬完，卡内部烧写仍占线（DAT0 拉低）——
 * 下一条数据命令前必须等它。超时 ~1s 量级（N 依 PIO 圈速）。
 */
static int mmc_wait_card_ready(struct mmc_card *card)
{
	u32 base = card->base;
	u32 rsp0;
	int ret;

	/* 10000 轮 ≈ 1~2s（eMMC 分区切换后首次写可能触发 FTL 初始化，
	 * 2000 轮不够用——板上实证 2026-11-20） */
	for (int to = 0; to < 10000; to++) {
		/* card->rca 本就是 R6 原始响应 & 0xFFFF0000（= RCA<<16 的
		 * arg 格式，CMD7/CMD9 同款直传）；再左移一次 = 广播 RCA 0，
		 * 卡永不响应（2026-11-13 板上实证） */
		ret = usdhc_send_cmd(base, 13, card->rca,
				     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
				     XFERTYP_CICEN, 0, &rsp0);
		if (ret) {
			printf("mmc: CMD13 fail ret=%d\n", ret);
			return -1;
		}
		/* R1 错误位群（规范 5.1；原掩码漏 WP_VIOLATION bit26——
		 * 烧录场景最相关的一位）。先查错再查就绪：卡拒绝写入
		 * （如写保护）时常同时置 WP_VIOLATION+READY_FOR_DATA，
		 * 顺序反了会把失败当成功（第四轮评审 M5） */
		if (rsp0 & 0xFFF98000U) {
			printf("mmc: card status err %08x\n", rsp0);
			return -2;
		}
		if (rsp0 & BIT(8))		/* READY_FOR_DATA */
			return 0;
	}
	printf("mmc: card not ready (CMD13 timeout)\n");
	return -1;
}

/*
 * R2（136 位响应）去移位：USDHC 捕获时整体多采 8 位（RSP0=CSD[39:8]，
 * 最低字节是响应前沿位，"lowest 8 bit is invalid bit"），须就地串接成
 * 规范布局后才能按规范位段解析。NXP SDK USDHC_ReceiveCommandResponse
 * 与第三方参考项目逐位同款（证据：fsl_usdhc.c R2 分支、
 * NoosProgramProject/14_TF卡编程 sd.c USDHC_GetResponse）。
 * 修前 SD 容量读出差 8 位（第四轮评审 H1，INFO 回显即现形）。
 */
static void r2_deshift(u32 raw[4])
{
	raw[3] = (raw[3] << 8) | ((raw[2] >> 24) & 0xFF);
	raw[2] = (raw[2] << 8) | ((raw[1] >> 24) & 0xFF);
	raw[1] = (raw[1] << 8) | ((raw[0] >> 24) & 0xFF);
	raw[0] <<= 8;
}

/* ---- 卡识别层 ---- */

/*
 * 本驱动使用的命令一览（SD 物理/eMMC 规范，与 RM 58.6 命令表同源）
 *
 * 命令   名称                  arg            响应      目的
 * ------ --------------------- -------------- --------- -------------------------
 * CMD0   GO_IDLE_STATE         0              无        软复位卡 -> Idle 态
 * CMD1   SEND_OP_COND          0x40FF8000     R3        eMMC 电压协商/等卡就绪
 * CMD8   SEND_IF_COND          0x1AA          R7        SD2.0 握手(回显图案 0xAA)
 * CMD55  APP_CMD               0              R1        应用命令前缀(下一句按
 *                                                         ACMD 解释)
 * ACMD41 SD_SEND_OP_COND       0x40FF8000     R3        SD 电压协商/等卡就绪
 * CMD2   ALL_SEND_CID          0              R2        收卡"身份证" -> Ident 态
 * CMD3   SEND/SET_REL_ADDR     SD:0/MMC:RCA   R6/R1     定址 RCA -> Standby 态
 * CMD9   SEND_CSD              RCA<<16        R2        收卡参数页(容量/时序)
 * CMD7   SELECT_CARD           RCA<<16        R1b       点名选中 -> Transfer 态
 *                                                         (此后数据命令才合法)
 * CMD17  READ_SINGLE_BLOCK     LBA            R1+数据   读一块(512B+CRC16)
 *
 * 参数位分解：
 *   CMD8   arg = [12:8] VHS=0x1 (2.7~3.6V) | [7:0] 图案 0xAA，R7 原样回显，
 *          回显不符 = SD1.x 老卡(不支持块寻址)，拒绝服务
 *   ACMD41/CMD1 arg = [30] HCS=1 (主机支持大容量) | [23:15] Vdd 窗 2.7~3.6V
 *          R3 返回 = [31] busy(上电中=0，就绪=1，轮询它) | [30] 卡容量 |
 *                    [23:0] 卡电压窗
 *   CMD3   SD：卡自选号，R6 响应 [31:16] 即新 RCA -> r & 0xFFFF0000
 *          eMMC：主机出号，arg = RCA(=1) << 16，R1 响应
 *   CMD17  arg = 块号(块寻址)。敢直接填 LBA 的前提 = CMD8 握手通过 + HCS=1；
 *          老 SDSC 卡此处应为字节地址，本驱动不支持(识别阶段已筛除)
 *
 * 响应类型 -> XFERTYP 位选择（规范决定，不是随手配）：
 *   R1/R6/R7 = RSPTYP_48  | CCCEN | CICEN  48 位：CRC7 + 命令号回显
 *   R1b      = RSPTYP_48_BUSY                  R1 + 卡拉 DAT0 示忙，硬件盯线
 *   R2       = RSPTYP_136 | CCCEN              136 位 CID/CSD：有 CRC、无命令号
 *   R3       = RSPTYP_48 (裸)                  OCR：规范规定无 CRC、无命令号
 *                                             回显——误开校验位会永远报错
 */


static int sd_cmd0(u32 base)
{
	return usdhc_send_cmd(base, 0, 0, 0, 0, NULL);
}

/* 毫秒级忙等（timer 8MHz 计数差值；GO_IDLE 时序用，对齐内核 mdelay(1)） */
static void emmc_delay_ms(u32 ms)
{
	u64 start = timer_counter();
	u64 ticks = (u64)ms * (timer_freq_hz() / 1000);

	while (timer_counter() - start < ticks)
		;
}

/* ---- 提速：4 位总线 + High Speed（v0.12）----
 * 时序属 SD/eMMC 规范层（RM 58.6 只给命令定义不给用法），对齐
 * U-Boot mmc core：先宽度（ACMD6 / CMD6 写 BUS_WIDTH），再时钟到
 * Default Speed 档（SD 25M / eMMC 26M），最后切 High Speed 提到 50M。
 * HS 切换失败降级留在 DS 继续启动（600KB 也就 ~250ms）；宽度和
 * DS 提速失败 = 卡或链路异常，返回错误由上层报停。*/
#define CLK_DS_HZ	25000000U	/* Default Speed 档上限 */
#define CLK_HS_HZ	50000000U	/* High Speed（就近分频 49.5M）*/

/**
 * sd_speed_up() - SD 卡：1 位 400kHz -> 4 位 50MHz High Speed
 * @rca: CMD3 拿到的卡相对地址（ACMD55 前缀要带它）
 *
 * SD 的 CMD6(SWITCH) 除 R1 外还在数据线回 64 字节状态——必须按
 * 单块 64B 数据读收掉，否则卡驱动数据线、下一条命令报错。
 */
static int sd_speed_up(u32 base, u32 rca)
{
	const struct usdhc_map *m = usdhc_map_get(base);
	u32 status[16];			/* CMD6 返回 64B 状态，只验 R1 不解析 */
	u32 r;
	int ret, hz;

	ret = usdhc_send_cmd(base, 55, rca,
			     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN | XFERTYP_CICEN,
			     0, &r);
	if (!ret) {
		ret = usdhc_send_cmd(base, 6, 2,		/* ACMD6: 4 位 */
				     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
				     XFERTYP_CICEN, 0, &r);
	}
	if (ret) {
		return -1;		/* 4 位是 SD 卡强制能力，失败即异常 */
	}
	writel((readl(m->prot_ctrl) & ~PROT_CTRL_DTW_MASK) |
	       PROT_CTRL_DTW_4BIT, m->prot_ctrl);

	hz = usdhc_set_clock(base, CLK_DS_HZ);
	if (hz < 0) {
		return -2;
	}

	/* CMD6 mode switch：arg=0x80|保留组 0xF|group1 选 1(High Speed)，
	 * R1 bit7 = switch error（SD 规范 CARD_STATUS 的 SWITCH_ERROR）*/
	writel((1 << 16) | 64, m->blk_att);
	ret = usdhc_send_cmd(base, 6, 0x80FFFFF1U,
			     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN | XFERTYP_CICEN |
			     XFERTYP_DPSEL,
			     MIX_CTRL_BCEN | MIX_CTRL_DTDSEL_READ, &r);
	if (ret || (r & BIT(7)) ||
	    usdhc_read_data_port(base, status, 64 / 4)) {
		printf("sd: HS switch failed, stay at DS\n");
		return 0;
	}

	hz = usdhc_set_clock(base, CLK_HS_HZ);
	if (hz < 0) {
		return -3;
	}
	printf("sd: 4-bit, %u kHz\n", (u32)hz / 1000);
	return 0;
}

/**
 * emmc_speed_up() - eMMC：1 位 400kHz -> 4 位 52MHz High Speed
 *
 * eMMC 的 CMD6 是 byte-mode 写 EXT_CSD（无数据相位）：
 * arg = 0x03<<24 | 索引<<16 | 值<<8，响应 R1b。
 */
static int emmc_speed_up(u32 base)
{
	const struct usdhc_map *m = usdhc_map_get(base);
	u32 r;
	int ret, hz;
	u32 to;

	/* EXT_CSD[183] BUS_WIDTH = 2（8 位，②）：主机与卡同时切宽度。
	 * 顺序对齐 U-Boot mmc core：DS 档下切宽度 → 再切 High Speed */
	ret = usdhc_send_cmd(base, 6, 0x03B70200U, XFERTYP_RSPTYP_48_BUSY,
			     0, &r);
	if (ret) {
		return -1;
	}
	if (r & BIT(24)) {
		printf("emmc: SWITCH_ERROR on BUS_WIDTH\n");
		return -1;
	}
	/* ★SWITCH 后等卡 busy 结束（DAT0 释放）才能动时钟——JEDEC 要求
	 * SWITCH 后轮询 busy；缺此步时紧跟的 set_clock 停钟会打断切换
	 * （2026-11-21 板上实证：BUS_WIDTH=2 写入后回读 0，HS_TIMING
	 * 侥幸存活）。DAT0 电平 = PSTATE.DLSL[0]（bit24） */
	to = 100000;
	while ((readl(m->pres_state) & PSTATE_DAT0_LVL) == 0 && --to)
		;				/* 等忙开始（R1b 后即刻） */
	to = 10000000;
	while ((readl(m->pres_state) & PSTATE_DAT0_LVL) && --to)
		;				/* 等忙结束（DAT0 释放） */
	writel((readl(m->prot_ctrl) & ~PROT_CTRL_DTW_MASK) |
	       PROT_CTRL_DTW_8BIT, m->prot_ctrl);

	hz = usdhc_set_clock(base, CLK_DS_HZ);
	if (hz < 0) {
		return -2;
	}

	/* EXT_CSD[185] HS_TIMING = 1（High Speed）*/
	ret = usdhc_send_cmd(base, 6, 0x03B90100U, XFERTYP_RSPTYP_48_BUSY,
			     0, &r);
	if (ret) {
		printf("emmc: HS switch failed, stay at DS\n");
		return 0;
	}
	if (r & BIT(24)) {
		printf("emmc: SWITCH_ERROR on HS_TIMING\n");
		return 0;
	}
	to = 100000;
	while ((readl(m->pres_state) & PSTATE_DAT0_LVL) == 0 && --to)
		;
	to = 10000000;
	while ((readl(m->pres_state) & PSTATE_DAT0_LVL) && --to)
		;

	hz = usdhc_set_clock(base, CLK_HS_HZ);
	if (hz < 0) {
		return -3;
	}

	/* 卡侧宽度回读（CMD8 重读 EXT_CSD[183]）：确认 BUS_WIDTH=2 真的
	 * 生效——主机 DTW 与卡宽度不一致时数据线错位，症状是慢而非错。
	 * CMD8 arg = RCA<<16（eMMC 主机定 RCA=1 → 0x00010000，emmc_init 同款） */
	writel((1 << 16) | MMC_BLOCK_SIZE, m->blk_att);
	if (!usdhc_send_cmd(base, 8, 0x00010000,
			    XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
			    XFERTYP_CICEN | XFERTYP_DPSEL,
			    MIX_CTRL_BCEN | MIX_CTRL_DTDSEL_READ, &r)) {
		u32 ext_check[MMC_BLOCK_SIZE / 4];

		if (!usdhc_read_data_port(base, ext_check,
					  MMC_BLOCK_SIZE / 4)) {
			const u8 *ec = (const u8 *)ext_check;

			printf("emmc: bus_width=%u hs_timing=%u (card side)\n",
			       ec[183], ec[185]);
		}
	}
	printf("emmc: 8-bit, %u kHz\n", (u32)hz / 1000);
	return 0;
}

/** SD 识别：CMD0 -> CMD8 -> ACMD41(轮询 busy) -> CMD2 -> CMD3 -> CMD9 -> CMD7 */
int sd_init(struct mmc_card *card)
{
	const struct usdhc_map *m;
	u32 base = USDHC1_BASE, r;
	int i, ret;

	m = usdhc_map_get(base);
	writel(readl(CCM_CCGR6) | (3 << 2), CCM_CCGR6);	/* usdhc1 门控 */
	pins_setup(sd1_pins, sizeof(sd1_pins) / sizeof(sd1_pins[0]));
	if (usdhc_hw_init(base)) {
		return -1;
	}

	sd_cmd0(base);

	/* CMD8: 电压检查 0x1AA，回显低 8 位为 0xAA 即 SD2.0+ */
	ret = usdhc_send_cmd(base, 8, 0x1AA,
			     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN | XFERTYP_CICEN,
			     0, &r);
	if (ret || (r & 0xFF) != 0xAA) {
		return -2;			/* 不是 SD2.0 卡或无卡 */
	}

	/* ACMD41 = CMD55 + ACMD41，忙位 OCR[31] 置 1 即就绪。
	 * 命令级错误（超时/CRC）= 无卡或线路问题，立即失败不空等 1000 轮 */
	for (i = 0; i < 1000; i++) {
		ret = usdhc_send_cmd(base, 55, 0,
				     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
				     XFERTYP_CICEN, 0, &r);
		if (ret) {
			return -3;
		}
		ret = usdhc_send_cmd(base, 41, 0x40FF8000,
				     XFERTYP_RSPTYP_48, 0, &r);
		if (ret) {
			return -3;
		}
		if (r & BIT(31)) {
			break;
		}
	}
	if (i == 1000) {
		return -3;
	}
	card->csd_raw[0] = r;	/* 暂存 OCR（调试打印用）*/

	usdhc_send_cmd(base, 2, 0, XFERTYP_RSPTYP_136 | XFERTYP_CCCEN,
		       0, NULL);				/* ALL_SEND_CID */
	r2_capture(card, m);					/* CID 身份位 */
	ret = usdhc_send_cmd(base, 3, 0,
			     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
			     XFERTYP_CICEN, 0, &r);
	if (ret) {
		return -4;
	}
	card->rca = r & 0xFFFF0000U;

	usdhc_send_cmd(base, 9, card->rca,
		       XFERTYP_RSPTYP_136 | XFERTYP_CCCEN, 0, NULL);	/* CSD */
	for (i = 0; i < 4; i++) {
		card->csd_raw[i] = readl(m->cmd_rsp0 + i * 4);
	}
	r2_deshift(card->csd_raw);		/* 去硬件 8 位偏移 -> 规范布局 */

	ret = usdhc_send_cmd(base, 7, card->rca, XFERTYP_RSPTYP_48_BUSY, 0,
			     NULL);		/* SELECT_CARD (R1b) */
	if (ret) {
		return -5;
	}

	ret = sd_speed_up(base, card->rca);
	if (ret) {
		return -6;
	}

	/* 详情采集（失败不挡烧录，字段留 0）：
	 * - CSD.TRAN_SPEED：位段 [103:96]，常见码表换算 MHz
	 * - SCR（ACMD51 读 8B）：SD_SPEC[63:60]/SD_SPEC3(bit47)/位宽[51:48]，
	 *   布局按 kernel drivers/mmc/core/sd.c 的 mmc_decode_scr 同位段 */
	{
		static const struct { u8 code, mhz; } ts_tab[] = {
			{ 0x32, 25 }, { 0x5A, 50 },
		};
		u32 scr[2] = { 0, 0 };
		u8 ts = UNSTUFF_BITS(card->csd_raw, 96, 8);

		for (u32 i = 0; i < 2; i++)
			if (ts_tab[i].code == ts)
				card->tran_speed_mhz = ts_tab[i].mhz;

		writel((1 << 16) | 8, m->blk_att);
		if (!usdhc_send_cmd(base, 55, card->rca,
				    XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
				    XFERTYP_CICEN, 0, &r) &&
		    !usdhc_send_cmd(base, 51, 0,
				    XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
				    XFERTYP_CICEN | XFERTYP_DPSEL,
				    MIX_CTRL_BCEN | MIX_CTRL_DTDSEL_READ,
				    &r) &&
		    !usdhc_read_data_port(base, scr, 2)) {
			card->sd_spec = scr[0] >> 28;		/* [63:60] */
			card->sd_spec3 = (scr[0] >> 15) & 1;	/* bit47 */
			card->bus_width_4 = ((scr[0] >> 16) & 0xF) & 2 ? 1 : 0;
		}
	}

	/*
	 * SD v2.0 容量：CSD[69:48] = C_SIZE（22 位），容量 = (C_SIZE+1)*512KB。
	 * csd_raw 已去移位（r2_deshift）为规范 CSD 布局，标准公式直用；
	 * INFO 会回显容量供人工核对（读回校验文化：解析错了立刻可见）。
	 */
	{
		u32 c_size = ((card->csd_raw[2] & 0x3F) << 16) |
			     ((card->csd_raw[1] >> 16) & 0xFFFF);
		card->blocks = (c_size + 1) * 1024;	/* (C_SIZE+1)*512KB */
		printf("sd: capacity %u blocks (%u MiB)\n", card->blocks,
		       card->blocks / 2048);
	}

	card->base = base;
	card->is_emmc = 0;
	return 0;
}

/** eMMC 识别：CMD0 -> CMD1(轮询 busy) -> CMD2 -> CMD3(RCA=1) -> CMD9 -> CMD7 */
int emmc_init(struct mmc_card *card)
{
	const struct usdhc_map *m;
	u32 base = USDHC2_BASE, r;
	int i, ret;

	m = usdhc_map_get(base);

	writel(readl(CCM_CCGR6) | (3 << 4), CCM_CCGR6);	/* usdhc2 门控 */
	pins_setup(emmc_pins, sizeof(emmc_pins) / sizeof(emmc_pins[0]));
	emmc_daisy_setup();			/* ★输入路径接通（根因修复） */
	if (usdhc_hw_init(base)) {
		printf("emmc: hw init fail\n");		/* 复位/时钟链 */
		return -1;
	}

	/* GO_IDLE 对齐 kernel mmc_go_idle 时序（mmc_ops.c:139）：前后各
	 * 1ms 延时；ret 检查（无响应命令也应有 CC）
	 * ret=-10 = 总线未空闲（控制器级），-1 = CC 不到 */
	emmc_delay_ms(1);
	ret = sd_cmd0(base);
	emmc_delay_ms(1);
	if (ret) {
		printf("emmc: CMD0 fail ret=%d\n", ret);
		return -1;
	}

	/* CMD1 两段式，逐条对齐 kernel（mmc.c:2258 + mmc_ops.c:176）：
	 * ① 首发 OCR=0 问询卡电压窗；② 此后用 响应|BIT(30) 反复发，
	 *    轮间 10ms，忙位 OCR[31] 置 1 即就绪。
	 * eMMC spec v5.1 §6.4.3（内核注释原文引）：busy 态被 CMD0 打过
	 * 的卡可能进 inactive——不反复 CMD1 就永远不应答。2026-11-19
	 * 板上实证正是此症：CMD0 OK 后 CMD1 无响应、线电平全高、无超时错 */
	{
		u32 ocr = 0;

		for (i = 0; i < 1000; i++) {
			ret = usdhc_send_cmd(base, 1, ocr,
					     XFERTYP_RSPTYP_48, 0, &r);
			if (ret) {
				printf("emmc: CMD1 fail ret=%d pstate=%08x int=%08x\n",
				       ret, readl(m->pres_state),
				       readl(m->int_status));
				return -2;
			}
			if (r & BIT(31)) {
				break;
			}
			if (!ocr) {
				ocr = r | BIT(30);	/* 问询转真请求 */
			}
			emmc_delay_ms(10);
		}
		if (i == 1000) {
			printf("emmc: CMD1 busy timeout, ocr=%08x\n", r);
			return -2;
		}
	}

	usdhc_send_cmd(base, 2, 0, XFERTYP_RSPTYP_136 | XFERTYP_CCCEN,
		       0, NULL);
	r2_capture(card, m);					/* CID 身份位 */
	ret = usdhc_send_cmd(base, 3, 0x00010000,	/* eMMC：主机定 RCA=1 */
			     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
			     XFERTYP_CICEN, 0, &r);
	if (ret) {
		printf("emmc: CMD3 fail ret=%d\n", ret);
		return -3;
	}
	card->rca = 0x00010000;

	usdhc_send_cmd(base, 9, card->rca,
		       XFERTYP_RSPTYP_136 | XFERTYP_CCCEN, 0, NULL);
	for (i = 0; i < 4; i++) {
		card->csd_raw[i] = readl(m->cmd_rsp0 + i * 4);
	}
	r2_deshift(card->csd_raw);		/* 去硬件 8 位偏移 -> 规范布局 */

	ret = usdhc_send_cmd(base, 7, card->rca, XFERTYP_RSPTYP_48_BUSY, 0,
			     NULL);
	if (ret) {
		printf("emmc: CMD7 fail ret=%d\n", ret);
		return -4;
	}

	ret = emmc_speed_up(base);
	if (ret) {
		printf("emmc: speed up fail ret=%d\n", ret);
		return -5;
	}

	/*
	 * eMMC 容量：CMD8 SEND_EXT_CSD -> SEC_COUNT[215:212]（LE）。
	 * CSD v3 对 >2GB 不含容量，必须走 EXT_CSD；数据口机制与读路径
	 * 同款（R1 + BRR 收 512B，bswap 归一后 w[53] 即 SEC_COUNT）。
	 */
	writel((1 << 16) | MMC_BLOCK_SIZE, m->blk_att);
	ret = usdhc_send_cmd(base, 8, card->rca,	/* 已是 arg 格式，勿再移 */
			     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
			     XFERTYP_CICEN | XFERTYP_DPSEL,
			     MIX_CTRL_BCEN | MIX_CTRL_DTDSEL_READ, &r);
	{
		u32 ext[MMC_BLOCK_SIZE / 4];

		if (ret || usdhc_read_data_port(base, ext, MMC_BLOCK_SIZE / 4)) {
			printf("emmc: EXT_CSD read failed, blocks unknown\n");
			card->blocks = 0;
		} else {
			const u8 *e = (const u8 *)ext;

			card->blocks = ext[53];
			printf("emmc: capacity %u blocks (%u MiB)\n",
			       card->blocks, card->blocks / 2048);
			/* 身份/健康摘录（偏移=kernel include/linux/mmc/mmc.h
			 * 同值；多字节字段 JEDEC 定死小端） */
			/* 健康字段提取（偏移=kernel include/linux/mmc/mmc.h
			 * 同值；多字节字段 JEDEC 定死小端。cache_kb 的四字节
			 * 组合读曾被 GCC -O2 合并为未对齐 ldr 导致 Data Abort
			 * 挂死——start.S 已设 SCTLR.U=1 允许不对齐，此症根治） */
			card->ext_rev = e[192];
			card->pre_eol = e[267];
			card->life_a = e[268];
			card->life_b = e[269];
			card->cache_kb = e[249] | (e[250] << 8) |
					 (e[251] << 16) | ((u32)e[252] << 24);
			card->boot_mult = e[226];

			/* ★启用写缓存（对齐 kernel mmc.c:1826-1845：
			 * "cache size > 0 表示缓存存在且可打开"，内核枚举时
			 * 主动 CMD6 写 CACHE_CTRL=1。写缓存的 READY 语义 =
			 * 数据进缓存即 ready，NAND 编程后台进行——≤64MB
			 * 目标写速从"每 chunk 排空"的 4MB/s 提到总线极限。
			 * CMD6 Write Byte：arg = Index<<16 | Value<<8 */
			if (card->cache_kb > 0) {
				writel((1 << 16) | MMC_BLOCK_SIZE,
				       m->blk_att);
				ret = usdhc_send_cmd(base, 6, 0x00210100,
						     XFERTYP_RSPTYP_48_BUSY,
						     0, &r);
				if (ret) {
					printf("emmc: cache enable fail "
					       "ret=%d（继续，无缓存烧录）\n",
					       ret);
				} else {
					mmc_wait_card_ready(card);
					card->cache_ctrl = 1;
					printf("emmc: write cache ON "
					       "(%u KB)\n", card->cache_kb);
				}
			}
		}
	}

	printf("emmc: init returning\n");			/* ★诊断 */
	card->base = base;
	card->is_emmc = 1;
	return 0;
}

/* ---- 块读取 ---- */

/* CMD18 多块读（MSBSEL+BCEN+AC12EN；BLK_ATT 带块数）：一条命令收完
 * nblocks，块间命令往返消失——读吞吐主要来源。单块也走此路径
 * （AC12EN 单块被硬件忽略，RM 58.8.19） */
int mmc_read_blocks(struct mmc_card *card, u32 lba, u32 nblocks, void *buf,
		     void (*progress)(u32 done, u32 total))
{
	u32 base = card->base;
	const struct usdhc_map *m = usdhc_map_get(base);
	int ret;

	writel((nblocks << 16) | MMC_BLOCK_SIZE, m->blk_att);
	/* ADMA2 读方向：硬件 卡→DDR，CPU 脱离 */
	mmc_adma2_setup(m, buf, nblocks * MMC_BLOCK_SIZE);
	ret = usdhc_send_cmd(base, 18, lba,
			     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
			     XFERTYP_CICEN | XFERTYP_DPSEL,
			     MIX_CTRL_DMAEN | MIX_CTRL_MSBSEL |
			     MIX_CTRL_BCEN | MIX_CTRL_DTDSEL_READ,
			     NULL);
	if (ret) {
		printf("mmc: CMD18 fail lba=%u n=%u ret=%d\n", lba, nblocks,
		       ret);
		return -1;
	}
	/* 等 TC（End 描述符完成） */
	if (!usdhc_wait_set(m->int_status, IRQSTAT_TC)) {
		printf("mmc: adma TC timeout, sts=%08x\n",
		       readl(m->int_status));
		mmc_recover(card);
		return -2;
	}
	writel(IRQSTAT_TC, m->int_status);
	/* 读完后手动 CMD12 停流（对齐 U-Boot，不用 AC12EN） */
	usdhc_send_cmd(base, 12, 0,
		       XFERTYP_RSPTYP_48_BUSY | XFERTYP_CMDTYP_ABORT,
		       0, NULL);
	if (progress) {
		progress(nblocks, nblocks);
	}
	return 0;
}

/**
 * mmc_write_blocks() - 向卡的用户区写入若干 512 字节块
 *
 * CMD25 多块写（MSBSEL+BCEN）：一条命令流式推 nblocks，块间
 * CMD24/CMD13 往返消失；卡编程忙由 DAT0 反压、FIFO 自动停发消化，
 * 收尾一次 CMD13 等编程完成。nblocks ≤ 16K chunk（download 层切好）。
 *
 * Return: 0 成功；-1 命令超时/错误；-2 数据错误；-3 卡编程未就绪；
 *         -4 越界/容量未知（烧录必须 fail closed，不许盲写）
 */
int mmc_write_blocks(struct mmc_card *card, u32 lba, u32 nblocks,
		     const void *buf, void (*progress)(u32 done, u32 total))
{
	u32 base = card->base;
	const struct usdhc_map *m = usdhc_map_get(base);
	u32 r1 = 0;
	int ret;

	/* 容量兜底（第四轮评审 L1）：JBDL 层有 H1 校验，但 EXT_CSD/CSD
	 * 解析失败时 blocks=0 会让整条防线失效——这里 fail closed */
	if (!card->blocks ||
	    (u64)lba + (u64)nblocks > card->blocks) {
		printf("mmc: write refuse lba=%u n=%u cap=%u\n",
		       lba, nblocks, card->blocks);
		return -4;
	}

	writel((nblocks << 16) | MMC_BLOCK_SIZE, m->blk_att);
	/* 分段计时（前 4 次写调用）：定位写路径耗时构成 */
	static u32 wt_n;				/* 已计时调用数 */
	u64 t0 = timer_counter();
	/* ADMA2：描述符表 → ADS_ADDR，MIX_CTRL.DMAEN 置位（随 mix_lo 写入）
	 * → CMD25 发出后硬件自动 DDR→卡，CPU 完全脱离数据路径 */
	mmc_adma2_setup(m, buf, nblocks * MMC_BLOCK_SIZE);
	ret = usdhc_send_cmd(base, 25, lba,
			     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
			     XFERTYP_CICEN | XFERTYP_DPSEL,
			     MIX_CTRL_DMAEN | MIX_CTRL_MSBSEL |
			     MIX_CTRL_BCEN,			/* DTDSEL=0 写 */
			     &r1);
	u64 t1 = timer_counter();
	if (ret) {
		printf("mmc: CMD25 fail lba=%u n=%u ret=%d r1=%08x\n",
		       lba, nblocks, ret, r1);
		return -1;
	}
	/* 等 TC（End 描述符完成 → TC 置位）；数据搬运全程硬件 */
	if (!usdhc_wait_set(m->int_status, IRQSTAT_TC)) {
		printf("mmc: adma TC timeout, sts=%08x\n",
		       readl(m->int_status));
		mmc_recover(card);
		return -2;
	}
	u64 t2 = timer_counter();
	/* 手动 CMD12 收尾（对齐 U-Boot mmc_write.c:166）：AC12EN 此前
	 * 依赖硬件自动发，但 RM 58.4.1.1 明文"出错时不发"且 CNIBAC12E
	 * 静默——手动发+等 R1b 可靠（agent 对拍 2026-11-20） */
	usdhc_send_cmd(base, 12, 0,
		       XFERTYP_RSPTYP_48_BUSY | XFERTYP_CMDTYP_ABORT,
		       0, NULL);
	u64 t3 = timer_counter();
	if (mmc_wait_card_ready(card)) {
		return -3;
	}
	u64 t4 = timer_counter();
	writel(IRQSTAT_TC, m->int_status);
	if (wt_n < 4) {
		printf("mmc: wt%u cmd25=%uus adma=%uus cmd12=%uus ready=%uus\n",
		       wt_n, udiv32((u32)(t1 - t0),
				    timer_freq_hz() / 1000000U),
		       udiv32((u32)(t2 - t1),
			      timer_freq_hz() / 1000000U),
		       udiv32((u32)(t3 - t2),
			      timer_freq_hz() / 1000000U),
		       udiv32((u32)(t4 - t3),
			      timer_freq_hz() / 1000000U));
	}
	wt_n++;
	if (progress) {
		progress(nblocks, nblocks);
	}
	return 0;
}

/* ---- 卡详情单行文本（INFO 的 media detail 行）----
 * CID 位段解码逐条对拍 kernel drivers/mmc/core/{sd,mmc}.c mmc_decode_cid：
 *   SD：PNM 5 字符、SN [24,32]、MDT [12,8]+[8,4]、基准年 2000
 *   eMMC：PNM 6 字符、SN [16,32]、PRV [48,8]、MDT [12,4]+[8,4]、基准年 1997
 * OEM 两字符按大端顺序取。PRV/HW.FW 高 4 位=主版本低 4 位=次版本 */
void mmc_card_detail(const struct mmc_card *card, char *buf, u32 size)
{
	const u32 *cid = card->cid_raw;
	char pnm[7];
	u32 i, mid, oem, serial, month, year;

	mid = UNSTUFF_BITS(cid, 120, 8);
	oem = UNSTUFF_BITS(cid, 104, 16);
	for (i = 0; i < (card->is_emmc ? 6u : 5u); i++)
		pnm[i] = UNSTUFF_BITS(cid, 96 - 8 * i, 8);
	pnm[i] = 0;

	if (card->is_emmc) {
		static const char *const revs[] = { "4.0", "4.1", "4.2",
						    "4.3", "4.4", "5.0", "5.1" };
		u32 prv = UNSTUFF_BITS(cid, 48, 8);

		serial = UNSTUFF_BITS(cid, 16, 32);
		month = UNSTUFF_BITS(cid, 12, 4);
		year = UNSTUFF_BITS(cid, 8, 4) + 1997;
		snprintf(buf, size,
			 "MID 0x%02x OEM %c%c PNM \"%s\" PRV %u.%u SN 0x%08x "
			 "MDT %u-%02u REV %s EOL %s LIFE A %u/10 B %u/10 "
			 "CACHE %uKB BOOTx%u",
			 mid, oem >> 8, oem & 0xFF, pnm, prv >> 4, prv & 0xF,
			 serial, year, month,
			 card->ext_rev <= 6 ? revs[card->ext_rev] : "?",
			 card->pre_eol == 1 ? "Normal" :
			 card->pre_eol == 2 ? "Warning" :
			 card->pre_eol == 3 ? "Urgent" : "-",
			 card->life_a, card->life_b, card->cache_kb,
			 card->boot_mult);
	} else {
		u32 hw = UNSTUFF_BITS(cid, 60, 4);
		u32 fw = UNSTUFF_BITS(cid, 56, 4);

		serial = UNSTUFF_BITS(cid, 24, 32);
		month = UNSTUFF_BITS(cid, 8, 4);
		year = UNSTUFF_BITS(cid, 12, 8) + 2000;
		snprintf(buf, size,
			 "MID 0x%02x OEM %c%c PNM \"%s\" HW %u FW %u "
			 "SN 0x%08x MDT %u-%02u CLK %uMHz SPEC %s BUS %s",
			 mid, oem >> 8, oem & 0xFF, pnm, hw, fw, serial,
			 year, month, card->tran_speed_mhz,
			 card->sd_spec == 0 ? "1.0" :
			 card->sd_spec == 1 ? "1.10" :
			 card->sd_spec3 ? "3.0" : "2.0",
			 card->bus_width_4 ? "4bit" : "1bit");
	}
}

/* ---- 通用块源实例：把卡协议驱动适配成 block_source 接口 ---- */

static struct mmc_card sd_card;
static struct mmc_card emmc_card;

static int sd_source_init(void)
{
	return sd_init(&sd_card);
}

static int sd_source_read(u32 lba, u32 nblocks, void *buf,
			  void (*progress)(u32 done, u32 total))
{
	return mmc_read_blocks(&sd_card, lba, nblocks, buf, progress);
}

static int sd_source_write(u32 lba, u32 nblocks, const void *buf,
			   void (*progress)(u32 done, u32 total))
{
	return mmc_write_blocks(&sd_card, lba, nblocks, buf, progress);
}

static u32 sd_source_blocks(void)
{
	return sd_card.blocks;
}

static char sd_detail_line[160];

static const char *sd_source_detail(void)
{
	mmc_card_detail(&sd_card, sd_detail_line, sizeof(sd_detail_line));
	return sd_detail_line;
}

const struct block_source sd_source = {
	.name = "SD (USDHC1)",
	.init = sd_source_init,
	.read = sd_source_read,
	.write = sd_source_write,
	.blocks = sd_source_blocks,
	.detail = sd_source_detail,
};

static int emmc_source_init(void)
{
	return emmc_init(&emmc_card);
}

static int emmc_source_read(u32 lba, u32 nblocks, void *buf,
			    void (*progress)(u32 done, u32 total))
{
	return mmc_read_blocks(&emmc_card, lba, nblocks, buf, progress);
}

static int emmc_source_write(u32 lba, u32 nblocks, const void *buf,
			     void (*progress)(u32 done, u32 total))
{
	return mmc_write_blocks(&emmc_card, lba, nblocks, buf, progress);
}

static u32 emmc_source_blocks(void)
{
	return emmc_card.blocks;
}

static char emmc_detail_line[160];

static const char *emmc_source_detail(void)
{
	mmc_card_detail(&emmc_card, emmc_detail_line,
			sizeof(emmc_detail_line));
	return emmc_detail_line;
}

/* 缓存 flush：CMD6 写 EXT_CSD[32]=1（FLUSH_CACHE）→ 卡把写缓存内容
 * 全部编程进 NAND，期间 DAT0 忙——内核 flush 超时上限 30s
 * （kernel mmc.c:31 CACHE_FLUSH_TIMEOUT_MS），我们轮询 10000 轮
 * ≈ 2s 量级（8GB 卡整缓存 flush 极端情形；实测按需再放宽） */
static int emmc_source_flush(void)
{
	u32 base = emmc_card.base;
	const struct usdhc_map *m = usdhc_map_get(base);
	u32 r = 0;
	int ret;

	if (!emmc_card.cache_ctrl)
		return 0;			/* 缓存没开，无易失数据 */
	writel((1 << 16) | MMC_BLOCK_SIZE, m->blk_att);
	/* CMD6 Write Byte：Index=32(FLUSH_CACHE) Value=1
	 * arg = 32<<16 | 1<<8 = 0x00200100 */
	ret = usdhc_send_cmd(base, 6, 0x00200100U,
			     XFERTYP_RSPTYP_48_BUSY, 0, &r);
	if (ret) {
		printf("emmc: cache flush CMD6 fail ret=%d\n", ret);
		return -1;
	}
	if (mmc_wait_card_ready(&emmc_card)) {
		printf("emmc: cache flush busy timeout\n");
		return -2;
	}
	printf("emmc: cache flushed\n");
	return 0;
}

const struct block_source emmc_source = {
	.name = "eMMC (USDHC2)",
	.init = emmc_source_init,
	.read = emmc_source_read,
	.write = emmc_source_write,
	.blocks = emmc_source_blocks,
	.detail = emmc_source_detail,
	.flush = emmc_source_flush,
};
