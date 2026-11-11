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
#include <mmc.h>
#include <math.h>
#include <stddef.h>
#include <printf.h>
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
#define IRQSTAT_BWTEN		BIT(4)
#define IRQSTAT_BRR		BIT(5)
#define IRQSTAT_CTOE		BIT(16)
#define IRQSTAT_CCE		BIT(17)
#define IRQSTAT_DTOE		BIT(20)
#define SYSCTL_RSTA		BIT(24)
#define SYSCTL_RSTC		BIT(25)
#define SYSCTL_RSTD		BIT(26)
#define SYSCTL_INITA		BIT(27)
#define PRSSTAT_SDSTB		BIT(3)		/* RM：改分频前必须等到它为 1 */
#define VENDORSPEC_CKEN		0x00004000U	/* RM 未给位表，值自 U-Boot 头 */
#define PROT_CTRL_DTW_MASK	0x00000006U	/* DTW[2:1]（编码同上 U-Boot）*/
#define PROT_CTRL_DTW_4BIT	0x00000002U
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

/* 无 libgcc 的除法已抽到 lib/math.c（udiv32/udiv64_mod），此处引用 */

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
	writel(readl(base + USDHC_VEND_SPEC_OFF) & ~VENDORSPEC_CKEN,
	       base + USDHC_VEND_SPEC_OFF);
	writel((readl(base + USDHC_SYS_CTRL_OFF) & ~0x0000FFF0U) |
	       (fs << 8) | (dvs << 4), base + USDHC_SYS_CTRL_OFF);
	if (!usdhc_wait_set(base, USDHC_PRES_STATE_OFF, PRSSTAT_SDSTB)) {
		return -1;
	}
	writel(readl(base + USDHC_VEND_SPEC_OFF) | VENDORSPEC_CKEN,
	       base + USDHC_VEND_SPEC_OFF);
	return (int)udiv32(base_hz, (dvs + 1) * fsdiv);
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

/** 从数据口收 nwords 个字到 buf（PIO，BRR 轮询；块长/块数由调用方设）*/
static int usdhc_read_data_port(u32 base, u32 *w, u32 nwords)
{
	if (!usdhc_wait_set(base, USDHC_INT_STATUS_OFF, IRQSTAT_BRR)) {
		return -1;
	}
	/* 数据端口是大端语义（板上实测 v0.5 MISMATCH → swab32 全匹配确诊），
	 * ARM 小端内存需在驱动边界归一化；将来写路径同样要 bswap */
	for (u32 i = 0; i < nwords; i++) {
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

/** 向数据口发 nwords 个字（PIO，BWTEN 轮询；块长/块数由调用方设）。
 * 字节序与读路径同款：数据口大端语义，写入前 swab */
static int usdhc_write_data_port(u32 base, const u32 *w, u32 nwords)
{
	if (!usdhc_wait_set(base, USDHC_INT_STATUS_OFF, IRQSTAT_BWTEN)) {
		return -1;
	}
	for (u32 i = 0; i < nwords; i++) {
		writel(__builtin_bswap32(w[i]), base + USDHC_DAT_PORT_OFF);
	}
	if (!usdhc_wait_set(base, USDHC_INT_STATUS_OFF, IRQSTAT_TC)) {
		return -2;
	}
	if (readl(base + USDHC_INT_STATUS_OFF) & (IRQSTAT_DTOE | IRQSTAT_CCE)) {
		return -2;
	}
	writel(IRQSTAT_TC | IRQSTAT_BWTEN, base + USDHC_INT_STATUS_OFF);
	return 0;
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

	for (int to = 0; to < 2000; to++) {
		ret = usdhc_send_cmd(base, 13, card->rca << 16,
				     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
				     XFERTYP_CICEN, 0, &rsp0);
		if (ret)
			return -1;
		/* R1 错误位群（规范 5.1；原掩码漏 WP_VIOLATION bit26——
		 * 烧录场景最相关的一位）。先查错再查就绪：卡拒绝写入
		 * （如写保护）时常同时置 WP_VIOLATION+READY_FOR_DATA，
		 * 顺序反了会把失败当成功（第四轮评审 M5） */
		if (rsp0 & 0xFFF98000U)
			return -2;
		if (rsp0 & BIT(8))		/* READY_FOR_DATA */
			return 0;
	}
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


static void sd_cmd0(u32 base)
{
	usdhc_send_cmd(base, 0, 0, 0, 0, NULL);
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
	writel((readl(base + USDHC_PROT_CTRL_OFF) & ~PROT_CTRL_DTW_MASK) |
	       PROT_CTRL_DTW_4BIT, base + USDHC_PROT_CTRL_OFF);

	hz = usdhc_set_clock(base, CLK_DS_HZ);
	if (hz < 0) {
		return -2;
	}

	/* CMD6 mode switch：arg=0x80|保留组 0xF|group1 选 1(High Speed)，
	 * R1 bit7 = switch error（SD 规范 CARD_STATUS 的 SWITCH_ERROR）*/
	writel((1 << 16) | 64, base + USDHC_BLK_ATT_OFF);
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
	u32 r;
	int ret, hz;

	/* EXT_CSD[183] BUS_WIDTH = 1（4 位）*/
	ret = usdhc_send_cmd(base, 6, 0x03B70100U, XFERTYP_RSPTYP_48_BUSY,
			     0, &r);
	if (ret) {
		return -1;
	}
	writel((readl(base + USDHC_PROT_CTRL_OFF) & ~PROT_CTRL_DTW_MASK) |
	       PROT_CTRL_DTW_4BIT, base + USDHC_PROT_CTRL_OFF);

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

	hz = usdhc_set_clock(base, CLK_HS_HZ);
	if (hz < 0) {
		return -3;
	}

	printf("emmc: 4-bit, %u kHz\n", (u32)hz / 1000);
	return 0;
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
	u32 base = USDHC2_BASE, r;
	int i, ret;

	writel(readl(CCGR6_REG) | (3 << 4), CCGR6_REG);	/* usdhc2 门控 */
	pins_setup(emmc_pins, sizeof(emmc_pins) / sizeof(emmc_pins[0]));
	if (usdhc_hw_init(base)) {
		return -1;
	}

	sd_cmd0(base);

	/* CMD1：主机 OCR（含 bit30 大容量标志），忙位 OCR[31] 置 1 即就绪。
	 * 命令级错误 = 无卡/线路问题，立即失败（不再空等 1000 轮）*/
	for (i = 0; i < 1000; i++) {
		ret = usdhc_send_cmd(base, 1, 0x40FF8000,
				     XFERTYP_RSPTYP_48, 0, &r);
		if (ret) {
			return -2;
		}
		if (r & BIT(31)) {
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
	r2_deshift(card->csd_raw);		/* 去硬件 8 位偏移 -> 规范布局 */

	ret = usdhc_send_cmd(base, 7, card->rca, XFERTYP_RSPTYP_48_BUSY, 0,
			     NULL);
	if (ret) {
		return -4;
	}

	ret = emmc_speed_up(base);
	if (ret) {
		return -5;
	}

	/*
	 * eMMC 容量：CMD8 SEND_EXT_CSD -> SEC_COUNT[215:212]（LE）。
	 * CSD v3 对 >2GB 不含容量，必须走 EXT_CSD；数据口机制与读路径
	 * 同款（R1 + BRR 收 512B，bswap 归一后 w[53] 即 SEC_COUNT）。
	 */
	writel((1 << 16) | MMC_BLOCK_SIZE, base + USDHC_BLK_ATT_OFF);
	ret = usdhc_send_cmd(base, 8, card->rca << 16,
			     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
			     XFERTYP_CICEN | XFERTYP_DPSEL,
			     MIX_CTRL_BCEN | MIX_CTRL_DTDSEL_READ, &r);
	{
		u32 ext[MMC_BLOCK_SIZE / 4];

		if (ret || usdhc_read_data_port(base, ext, MMC_BLOCK_SIZE / 4)) {
			printf("emmc: EXT_CSD read failed, blocks unknown\n");
			card->blocks = 0;
		} else {
			card->blocks = ext[53];
			printf("emmc: capacity %u blocks (%u MiB)\n",
			       card->blocks, card->blocks / 2048);
		}
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
		if (usdhc_read_data_port(base, p, MMC_BLOCK_SIZE / 4)) {
			return -2;
		}
		p += MMC_BLOCK_SIZE / 4;
		if (progress) {
			progress(i + 1, nblocks);
		}
	}
	return 0;
}

/**
 * mmc_write_blocks() - 向卡的用户区写入若干 512 字节块
 *
 * CMD24 逐块写（与读路径 CMD17 同款 PIO 风格，多块 CMD25 是后续课题）。
 * 每块后等卡编程完成（CMD13 READY_FOR_DATA）再发下一块。
 *
 * Return: 0 成功；-1 命令超时/错误；-2 数据错误；-3 卡编程未就绪；
 *         -4 越界/容量未知（烧录必须 fail closed，不许盲写）
 */
int mmc_write_blocks(struct mmc_card *card, u32 lba, u32 nblocks,
		     const void *buf, void (*progress)(u32 done, u32 total))
{
	u32 base = card->base;
	const u32 *p = buf;
	int ret;

	/* 容量兜底（第四轮评审 L1）：JBDL 层有 H1 校验，但 EXT_CSD/CSD
	 * 解析失败时 blocks=0 会让整条防线失效——这里 fail closed */
	if (!card->blocks ||
	    (u64)lba + (u64)nblocks > card->blocks) {
		printf("mmc: write refuse lba=%u n=%u cap=%u\n",
		       lba, nblocks, card->blocks);
		return -4;
	}

	for (u32 i = 0; i < nblocks; i++) {
		writel((1 << 16) | MMC_BLOCK_SIZE, base + USDHC_BLK_ATT_OFF);
		ret = usdhc_send_cmd(base, 24, lba + i,
				     XFERTYP_RSPTYP_48 | XFERTYP_CCCEN |
				     XFERTYP_CICEN | XFERTYP_DPSEL,
				     MIX_CTRL_BCEN,		/* DTDSEL=0 写方向 */
				     NULL);
		if (ret) {
			return -1;
		}
		if (usdhc_write_data_port(base, p, MMC_BLOCK_SIZE / 4)) {
			return -2;
		}
		p += MMC_BLOCK_SIZE / 4;
		if (mmc_wait_card_ready(card)) {
			return -3;
		}
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

static int sd_source_write(u32 lba, u32 nblocks, const void *buf,
			   void (*progress)(u32 done, u32 total))
{
	return mmc_write_blocks(&sd_card, lba, nblocks, buf, progress);
}

static u32 sd_source_blocks(void)
{
	return sd_card.blocks;
}

const struct block_source sd_source = {
	.name = "SD (USDHC1)",
	.init = sd_source_init,
	.read = sd_source_read,
	.write = sd_source_write,
	.blocks = sd_source_blocks,
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

const struct block_source emmc_source = {
	.name = "eMMC (USDHC2)",
	.init = emmc_source_init,
	.read = emmc_source_read,
	.write = emmc_source_write,
	.blocks = emmc_source_blocks,
};
