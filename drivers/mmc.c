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
#include <blocksrc.h>

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
#define USDHC_SIG_EN_OFF	0x38

/* INT_STATUS_EN：bit0-5 正常事件(CC/TC/BGE/DINT/BWR/BRR)，bit16-22 错误事件
 * (CTOE/CCE/CEBE/CIE/DTOE/DCE/DEBE)——错误位必须开，轮询判错才成立 */
#define IRQEN_NORMAL		0x0000003FU
#define IRQEN_ERRORS		0x007F0000U
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

/* 无 libgcc 的 32 位无符号除法（移位减法，内核 do_div 同思路）：
 * 裸机 -nostdlib 下 gcc 的 '/' 会引入 __aeabi_uidiv（libgcc），而该库的
 * 除零路径又依赖 raise()——自带实现一刀两断。d=0 无定义，调用方保证非零 */
static u32 udiv32(u32 n, u32 d)
{
	u32 q = 0;
	u32 bit = 1;

	while ((d < n) && !(d & 0x80000000U)) {
		d <<= 1;
		bit <<= 1;
	}
	while (bit) {
		if (n >= d) {
			n -= d;
			q |= bit;
		}
		d >>= 1;
		bit >>= 1;
	}
	return q;
}

/**
 * usdhc_set_clock() - 把 SD 时钟设到 <=target（就近选取分频）
 *
 * 基准时钟从 HOST_CAP 寄存器运行时读出（SDHCI 惯例，免 CCM 逆推），
 * 分频 sd_clk = base / ((DVS+1) * (2*SDCLKFS))。
 */
/* 基准时钟运行时计算，算法对齐 kernel clk-imx6ul.c:39,248-249,299-300：
 *   usdhc root = PLL2 PFD（CSCMR1 bit16/17 mux：0=pll2_pfd2 396M, 1=pll2_pfd0 352M）
 *              ÷ podf（CSCDR1：usdhc1 bit11、usdhc2 bit16，3 位 ÷1~8）
 * 注意：HOST_CAP[15:8] 在 i.MX 上是 retuning 能力位而非 SDHCI 标准的
 * base-clock 字段，不可用于读频率（v0.5.2 曾误读）。 */
static u32 usdhc_base_clock_mhz(u32 base)
{
	u32 is_usdhc1 = (base == USDHC1_BASE);
	u32 sel = (readl(MX6ULL_CCM_BASE + 0x1C) >>
		   (is_usdhc1 ? 16 : 17)) & 1;
	u32 podf = ((readl(MX6ULL_CCM_BASE + 0x24) >>
		     (is_usdhc1 ? 11 : 16)) & 7) + 1;

	return udiv32(sel ? 352 : 396, podf);
}

static void usdhc_set_clock(u32 base, u32 target_hz)
{
	u32 base_mhz = usdhc_base_clock_mhz(base);
	u32 sys = readl(base + USDHC_SYS_CTRL_OFF) & ~0x0000FFF0U;
	u32 dvs, fs;
	/* 从最小分频向大搜索：首个 <=target 的组合即"不超目标的最大频率"。
	 * （v0.5 曾从最大分频端搜索，恒命中 Base/8192≈24kHz，比最优慢 16 倍）*/
	for (dvs = 0; dvs <= 15; dvs++) {
		for (fs = 1; fs <= 256; fs++) {
			if (udiv32(base_mhz * 1000000U,
				 (dvs + 1) * 2 * fs) <= target_hz) {
				goto found;
			}
		}
	}
	dvs = 15;			/* 兜底（Base 低到无解时取最小频率）*/
	fs = 256;
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

	/* 状态使能开正常+错误事件；信号使能显式清 0（不依赖复位默认）——纯轮询 */
	writel(IRQEN_NORMAL | IRQEN_ERRORS, base + USDHC_INT_EN_OFF);
	writel(0, base + USDHC_SIG_EN_OFF);
	writel(0xFFFFFFFF, base + USDHC_INT_STATUS_OFF);/* 写 1 清中断状态历史 */
	writel((1 << 16) | MMC_BLOCK_SIZE, base + USDHC_BLK_ATT_OFF);/* 块长 512，count 占位 */
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

const struct block_source sd_source = {
	.name = "SD (USDHC1)",
	.init = sd_source_init,
	.read = sd_source_read,
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

const struct block_source emmc_source = {
	.name = "eMMC (USDHC2)",
	.init = emmc_source_init,
	.read = emmc_source_read,
};
