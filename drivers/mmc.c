// SPDX-License-Identifier: MIT
/*
 * justiceBL SD/MMC 子系统 —— USDHC 主机驱动 + SD/eMMC 卡识别。
 *
 * 依据（按项目规矩先读手册与参考后下手）：
 *   - RM uSDHC 章节；寄存器布局与 MCIMX6Y2.h 的 USDHC_Type 一致
 *   - U-Boot drivers/mmc/fsl_esdhc_imx.c/.h（XFERTYP 高 16 位照常、低 7 位
 *     数据 mode 位写 MIX_CTRL；VENDORSPEC_INIT=0x20007809；IRQ/BRR 等位值）
 *   - 第三方参考 imx6ull_NoosProgramProject/14_TF卡编程（CMD 标志位编码交叉验证）
 *   - 引脚/门控：kernel dts imx6ull-alientek-emmc.dts:633,666（pad 值）、
 *     imx6ul-pinfunc.h（偏移）、clk-imx6ul.c:446（CCGR6 bit2=usdhc1, bit4=usdhc2）
 *
 * 刻意的保守取舍（提速是后续课题）：1 位总线、400kHz、逐块 CMD17 PIO。
 * 所有等待均有界——本项目铁律：任何路径都不许死循环。
 */

#include <types.h>
#include <imx6ull.h>
#include <mmc.h>
#include <stddef.h>

/* ---- USDHC 寄存器偏移（USDHC_Type）---- */
#define USDHC_DS_ADDR_OFF	0x00
#define USDHC_BLK_ATT_OFF	0x04
#define USDHC_CMD_ARG_OFF	0x08
#define USDHC_CMD_XFR_OFF	0x0C
#define USDHC_RSP0_OFF		0x10
#define USDHC_DAT_PORT_OFF	0x20
#define USDHC_PRES_STATE_OFF	0x24
#define USDHC_PROT_CTRL_OFF	0x28
#define USDHC_SYS_CTRL_OFF	0x2C
#define USDHC_INT_STATUS_OFF	0x30
#define USDHC_INT_EN_OFF	0x34
#define USDHC_HOST_CAP_OFF	0x40
#define USDHC_MIX_CTRL_OFF	0x48
#define USDHC_VEND_SPEC_OFF	0xC0

/* ---- 位定义（值与 U-Boot fsl_esdhc_imx.h 一致）---- */
#define IRQSTAT_CC		BIT(0)
#define IRQSTAT_TC		BIT(1)
#define IRQSTAT_BRR		BIT(5)
#define IRQSTAT_CTOE		BIT(16)
#define IRQSTAT_CCE		BIT(17)
#define IRQSTAT_DTOE		BIT(20)
#define SYSCTL_RSTA		BIT(24)
#define SYSCTL_RSTC		BIT(25)
#define SYSCTL_RSTD		BIT(26)
#define SYSCTL_INITA		BIT(27)
#define VENDORSPEC_INIT		0x20007809U
#define VENDORSPEC_HCKEN	0x00001000U
#define VENDORSPEC_IPGEN	0x00000800U
#define XFERTYP_RSPTYP_136	BIT(16)
#define XFERTYP_RSPTYP_48	(2 << 16)
#define XFERTYP_RSPTYP_48_BUSY	(3 << 16)
#define XFERTYP_CCCEN		BIT(19)
#define XFERTYP_CICEN		BIT(20)
#define XFERTYP_DPSEL		BIT(21)
#define MIX_CTRL_BCEN		BIT(1)
#define MIX_CTRL_DTDSEL_READ	BIT(4)

#define USDHC1_BASE		0x02190000
#define USDHC2_BASE		0x02194000
#define CCGR6_REG		(MX6ULL_CCM_BASE + 0x80)

#define MMC_BLOCK_SIZE		512
#define WAIT_LOOPS		2000000U	/* 有界等待上限，约百毫秒级 */

/* ---- 板级引脚（偏移源自 imx6ul-pinfunc.h，pad 值源自 alientek dts）---- */
struct pin_cfg {
	u16 mux_off;
	u16 pad_off;
	u8 mux_mode;
	u32 pad_val;
};

static const struct pin_cfg sd1_pins[] = {	/* TF 卡，USDHC1 */
	{ 0x01C0, 0x044C, 0, 0x10071 },		/* SD1_CLK */
	{ 0x01BC, 0x0448, 0, 0x17059 },		/* SD1_CMD */
	{ 0x01C4, 0x0450, 0, 0x17059 },		/* SD1_DATA0 */
	{ 0x01C8, 0x0454, 0, 0x17059 },		/* SD1_DATA1 */
	{ 0x01CC, 0x0458, 0, 0x17059 },		/* SD1_DATA2 */
	{ 0x01D0, 0x045C, 0, 0x17059 },		/* SD1_DATA3 */
};

static const struct pin_cfg emmc_pins[] = {	/* eMMC，USDHC2（NAND 复用脚）*/
	{ 0x0178, 0x0404, 1, 0x10069 },		/* NAND_RE_B -> CLK */
	{ 0x017C, 0x0408, 1, 0x17059 },		/* NAND_WE_B -> CMD */
	{ 0x0180, 0x040C, 1, 0x17059 },		/* DATA0 */
	{ 0x0184, 0x0410, 1, 0x17059 },		/* DATA1 */
	{ 0x0188, 0x0414, 1, 0x17059 },		/* DATA2 */
	{ 0x018C, 0x0418, 1, 0x17059 },		/* DATA3 */
};

static void pins_setup(const struct pin_cfg *pins, u32 n)
{
	for (u32 i = 0; i < n; i++) {
		writel(pins[i].mux_mode, MX6ULL_IOMUXC_BASE + pins[i].mux_off);
		writel(pins[i].pad_val, MX6ULL_IOMUXC_BASE + pins[i].pad_off);
	}
}

/* ---- 主机层 ---- */

/** 等寄存器某些位变 0/变 1，有界；1=等到，0=超时 */
static int usdhc_wait_clear(u32 base, u32 off, u32 mask)
{
	u32 t = WAIT_LOOPS;

	while (readl(base + off) & mask) {
		if (!--t) {
			return 0;
		}
	}
	return 1;
}

static int usdhc_wait_set(u32 base, u32 off, u32 mask)
{
	u32 t = WAIT_LOOPS;

	while (!(readl(base + off) & mask)) {
		if (!--t) {
			return 0;
		}
	}
	return 1;
}

/**
 * usdhc_set_clock() - 把 SD 时钟设到 <=target（就近选取分频）
 *
 * 基准时钟从 HOST_CAP 寄存器运行时读出（SDHCI 惯例，免 CCM 逆推），
 * 分频 sd_clk = base / ((DVS+1) * (2*SDCLKFS))。
 */
static void usdhc_set_clock(u32 base, u32 target_hz)
{
	u32 base_mhz = (readl(base + USDHC_HOST_CAP_OFF) >> 8) & 0xFF;
	u32 sys = readl(base + USDHC_SYS_CTRL_OFF) & ~0x0000FFF0U;
	u32 dvs, fs;

	if (!base_mhz) {
		base_mhz = 24;		/* 兜底假设 24MHz */
	}
	for (dvs = 15; ; dvs--) {
		for (fs = 256; fs >= 1; fs--) {
			if ((base_mhz * 1000000U) /
			    ((dvs + 1) * 2 * fs) <= target_hz) {
				goto found;
			}
		}
		if (!dvs) {
			fs = 1;
			goto found;
		}
	}
found:
	writel(sys | (fs << 8) | (dvs << 4), base + USDHC_SYS_CTRL_OFF);
}

/** 主机复位 + 通用初始化（U-Boot fsl_esdhc_init 同套路）*/
static int usdhc_hw_init(u32 base)
{
	writel(readl(base + USDHC_SYS_CTRL_OFF) |
	       SYSCTL_RSTA | SYSCTL_RSTC | SYSCTL_RSTD,
	       base + USDHC_SYS_CTRL_OFF);
	if (!usdhc_wait_clear(base, USDHC_SYS_CTRL_OFF,
			      SYSCTL_RSTA | SYSCTL_RSTC | SYSCTL_RSTD)) {
		return -1;
	}

	writel(0, base + USDHC_MIX_CTRL_OFF);
	writel(VENDORSPEC_INIT, base + USDHC_VEND_SPEC_OFF);
	writel(readl(base + USDHC_VEND_SPEC_OFF) |
	       VENDORSPEC_HCKEN | VENDORSPEC_IPGEN,
	       base + USDHC_VEND_SPEC_OFF);

	/* 发 80 个初始化时钟（INITA 自清）*/
	writel(readl(base + USDHC_SYS_CTRL_OFF) | SYSCTL_INITA,
	       base + USDHC_SYS_CTRL_OFF);
	if (!usdhc_wait_clear(base, USDHC_SYS_CTRL_OFF, SYSCTL_INITA)) {
		return -1;
	}

	writel(0x0000803F, base + USDHC_INT_EN_OFF);	/* 状态使能，信号门全关（轮询）*/
	writel(0xFFFFFFFF, base + USDHC_INT_STATUS_OFF);/* 写 1 清历史 */
	writel((1 << 16) | 64, base + USDHC_BLK_ATT_OFF);/* 块大小 512，count 占位 */
	writel(0x00000008, base + USDHC_PROT_CTRL_OFF);	/* 1 位总线 + 大端数据口 */
	usdhc_set_clock(base, 400000);
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
	if (!usdhc_wait_clear(base, USDHC_PRES_STATE_OFF, BIT(0))) {
		return -1;	/* CMDINHIB：命令线忙 */
	}
	writel(0xFFFFFFFF, base + USDHC_INT_STATUS_OFF);
	writel(arg, base + USDHC_CMD_ARG_OFF);
	writel((readl(base + USDHC_MIX_CTRL_OFF) & 0xFFFFFF80U) | mix_lo,
	       base + USDHC_MIX_CTRL_OFF);
	writel(((u32)idx << 24) | xfr_hi, base + USDHC_CMD_XFR_OFF);

	if (!usdhc_wait_set(base, USDHC_INT_STATUS_OFF, IRQSTAT_CC)) {
		return -1;
	}
	if (readl(base + USDHC_INT_STATUS_OFF) &
	    (IRQSTAT_CTOE | IRQSTAT_CCE)) {
		return -2;
	}
	if (rsp0) {
		*rsp0 = readl(base + USDHC_RSP0_OFF);
	}
	return 0;
}

/** 收一块数据到 buf（512B，PIO 经数据口，BRR 轮询）*/
static int usdhc_read_data_port(u32 base, void *buf)
{
	u32 *w = buf;

	if (!usdhc_wait_set(base, USDHC_INT_STATUS_OFF, IRQSTAT_BRR)) {
		return -1;
	}
	/* 数据端口是大端语义（板上实测 v0.5 MISMATCH → swab32 全匹配确诊），
	 * ARM 小端内存需在驱动边界归一化；将来写路径同样要 bswap */
	for (u32 i = 0; i < MMC_BLOCK_SIZE / 4; i++) {
		w[i] = __builtin_bswap32(readl(base + USDHC_DAT_PORT_OFF));
	}
	if (!usdhc_wait_set(base, USDHC_INT_STATUS_OFF, IRQSTAT_TC)) {
		return -2;
	}
	if (readl(base + USDHC_INT_STATUS_OFF) &
	    (IRQSTAT_DTOE | IRQSTAT_CCE)) {
		return -2;
	}
	writel(IRQSTAT_TC | IRQSTAT_BRR, base + USDHC_INT_STATUS_OFF);
	return 0;
}

/* ---- 卡识别层 ---- */

static void sd_cmd0(u32 base)
{
	usdhc_send_cmd(base, 0, 0, 0, 0, NULL);
}

/** SD 识别：CMD0 -> CMD8 -> ACMD41(轮询 busy) -> CMD2 -> CMD3 -> CMD9 -> CMD7 */
int sd_init(struct mmc_card *card)
{
	u32 base = USDHC1_BASE, r;
	int i, ret;

	writel(readl(CCGR6_REG) | (3 << 2), CCGR6_REG);	/* usdhc1 门控 */
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

	/* ACMD41 = CMD55 + ACMD41，忙位 OCR[31] 置 1 即就绪 */
	for (i = 0; i < 1000; i++) {
		usdhc_send_cmd(base, 55, 0,
			       XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
			       XFERTYP_CICEN, 0, &r);
		ret = usdhc_send_cmd(base, 41, 0x40FF8000,
				     XFERTYP_RSPTYP_48, 0, &r);
		if (!ret && (r & BIT(31))) {
			break;
		}
	}
	if (i == 1000) {
		return -3;
	}
	card->csd_raw[0] = r;	/* 暂存 OCR（调试打印用）*/

	usdhc_send_cmd(base, 2, 0, XFERTYP_RSPTYP_136 | XFERTYP_CCCEN,
		       0, NULL);				/* ALL_SEND_CID */
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
		card->csd_raw[i] = readl(base + USDHC_RSP0_OFF + i * 4);
	}
	/* 注意：R2 的 4 个字是大端序，本驱动只透传原始值 */

	ret = usdhc_send_cmd(base, 7, card->rca, XFERTYP_RSPTYP_48_BUSY, 0,
			     NULL);		/* SELECT_CARD (R1b) */
	if (ret) {
		return -5;
	}

	card->base = base;
	card->is_emmc = 0;
	return 0;
}

/** eMMC 识别：CMD0 -> CMD1(轮询 busy) -> CMD2 -> CMD3(RCA=1) -> CMD9 -> CMD7 */
int emmc_init(struct mmc_card *card)
{
	u32 base = USDHC2_BASE, r;
	int i, ret;

	writel(readl(CCGR6_REG) | (3 << 4), CCGR6_REG);	/* usdhc2 门控 */
	pins_setup(emmc_pins, sizeof(emmc_pins) / sizeof(emmc_pins[0]));
	if (usdhc_hw_init(base)) {
		return -1;
	}

	sd_cmd0(base);

	/* CMD1：主机 OCR（含 bit30 大容量标志），忙位 OCR[31] 置 1 即就绪 */
	for (i = 0; i < 1000; i++) {
		ret = usdhc_send_cmd(base, 1, 0x40FF8000,
				     XFERTYP_RSPTYP_48, 0, &r);
		if (!ret && (r & BIT(31))) {
			break;
		}
	}
	if (i == 1000) {
		return -2;
	}

	usdhc_send_cmd(base, 2, 0, XFERTYP_RSPTYP_136 | XFERTYP_CCCEN,
		       0, NULL);
	ret = usdhc_send_cmd(base, 3, 0x00010000,	/* eMMC：主机定 RCA=1 */
			     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
			     XFERTYP_CICEN, 0, &r);
	if (ret) {
		return -3;
	}
	card->rca = 0x00010000;

	usdhc_send_cmd(base, 9, card->rca,
		       XFERTYP_RSPTYP_136 | XFERTYP_CCCEN, 0, NULL);
	for (i = 0; i < 4; i++) {
		card->csd_raw[i] = readl(base + USDHC_RSP0_OFF + i * 4);
	}

	ret = usdhc_send_cmd(base, 7, card->rca, XFERTYP_RSPTYP_48_BUSY, 0,
			     NULL);
	if (ret) {
		return -4;
	}

	card->base = base;
	card->is_emmc = 1;
	return 0;
}

/* ---- 块读取 ---- */

int mmc_read_blocks(struct mmc_card *card, u32 lba, u32 nblocks, void *buf,
		     void (*progress)(u32 done, u32 total))
{
	u32 base = card->base;
	u32 *p = buf;
	int ret;

	for (u32 i = 0; i < nblocks; i++) {
		writel((1 << 16) | MMC_BLOCK_SIZE, base + USDHC_BLK_ATT_OFF);
		ret = usdhc_send_cmd(base, 17, lba + i,
				     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
				     XFERTYP_CICEN | XFERTYP_DPSEL,
				     MIX_CTRL_BCEN | MIX_CTRL_DTDSEL_READ,
				     NULL);
		if (ret) {
			return -1;
		}
		if (usdhc_read_data_port(base, p)) {
			return -2;
		}
		p += MMC_BLOCK_SIZE / 4;
		if (progress) {
			progress(i + 1, nblocks);
		}
	}
	return 0;
}
