// SPDX-License-Identifier: MIT
/*
 * justiceBL 时间线记录 —— ARM 架构系统计数器（CNTPCT）实现。
 *
 * 使能序照抄 U-Boot arch/arm/mach-imx/syscounter.c（本板现役 U-Boot
 * 的 timer_init，板级 .config SYSCOUNTER_TIMER=y）：
 *   mcr CNTFRQ <- 8MHz；writel(CNTFID0, 8MHz)；
 *   CNTCR = (旧值 & ~FCREQ) | FREQ0|ENABLE|HDBG
 *
 * 频率 8MHz 为固定值（U-Boot mx6_common.h CONFIG_SC_TIMER_CLK），
 * 计数源在 SCTR 块内分频得到，与 CCM 时钟树配置无关。
 * 已上板验证（v0.11）：打点夹 uart_puts 字符数×86.8us 对账吻合。
 */

#include <types.h>
#include <imx6ull.h>
#include <timer.h>
#include <uart.h>
#include <printf.h>
#include <math.h>
#include <stddef.h>

/* ---- SCTR（SP807 System Counter 控制块，0x021DC000）----
 * 寄存器偏移照 U-Boot systimer.h sctr_regs；RM 未给寄存器章
 * （ARM IP，文档在 ARM TRM，树内 U-Boot 即唯一权威参考）。*/
#define SCTR_CNTCR_OFF		0x00	/* 控制：ENABLE/HDBG/FCREQ */
#define SCTR_CNTFID0_OFF	0x20	/* 频率 ID：CNTCR.FCREQ=01 时选中 */
#define SC_CNTCR_ENABLE		BIT(0)
#define SC_CNTCR_HDBG		BIT(1)	/* 调试挂起时计数停走（U-Boot 同款）*/
#define SC_CNTCR_FREQ0		BIT(8)	/* FCREQ=01：计数频率取 CNTFID0 */
#define SC_CNTCR_FREQ1		BIT(9)

#define SCTR_FREQ_HZ		8000000U	/* 固定 8MHz（U-Boot CONFIG_SC_TIMER_CLK）*/

/* SCTR 寄存器访问时钟门控：CCGR5[CG1]（RM 时钟图 SCTR_CLK_ENABLE）*/
#define CCGR5_REG		(MX6ULL_CCM_BASE + 0x7C)

/* ---- 模块状态 ---- */
static u32 timer_freq;

/** 读架构计数器 CNTPCT——64 位原子读，无 MMIO 高低半撕裂问题 */
static u64 read_cntpct(void)
{
	u32 lo, hi;

	__asm__ volatile ("mrrc p15, 0, %0, %1, c14" : "=r" (lo), "=r" (hi));
	return ((u64)hi << 32) | lo;
}

/**
 * timer_init() - 使能系统计数器并回读自检（静默，失败返负值）
 *
 * 返回 0 = 计数器已确认走字；-1/-2/-3 = 门控/CNTCR/计数三段
 * 自检对应失败（此时时间线不可信，main 报错停下）。
 */
int timer_init(void)
{
	u64 c0, c1;

	/* 1. 开 SCTR 寄存器访问时钟（CCGR5[CG1]，保险写：复位值未知）*/
	writel(readl(CCGR5_REG) | (3 << 2), CCGR5_REG);

	/* 2. 发布频率：CNTFRQ（CP15，给后续读架构定时器的软件）+ CNTFID0 */
	__asm__ volatile ("mcr p15, 0, %0, c14, c0, 0" :: "r" (SCTR_FREQ_HZ));
	writel(SCTR_FREQ_HZ, MX6ULL_SCTR_BASE + SCTR_CNTFID0_OFF);

	/* 3. 使能计数器：FCREQ=01 选中 CNTFID0 作计数频率 */
	writel((readl(MX6ULL_SCTR_BASE + SCTR_CNTCR_OFF) &
		~(SC_CNTCR_FREQ0 | SC_CNTCR_FREQ1)) |
	       SC_CNTCR_FREQ0 | SC_CNTCR_ENABLE | SC_CNTCR_HDBG,
	       MX6ULL_SCTR_BASE + SCTR_CNTCR_OFF);

	/* 4. 回读闭环：门控落盘、控制位落盘、计数确在递增 */
	if ((readl(CCGR5_REG) & (3 << 2)) != (3 << 2)) {
		return -1;
	}
	if ((readl(MX6ULL_SCTR_BASE + SCTR_CNTCR_OFF) &
	     (SC_CNTCR_ENABLE | SC_CNTCR_FREQ0)) !=
	    (SC_CNTCR_ENABLE | SC_CNTCR_FREQ0)) {
		return -2;
	}
	c0 = read_cntpct();
	for (volatile int i = 0; i < 100000; i++) {
		;
	}
	c1 = read_cntpct();
	if (c1 == c0) {
		return -3;
	}

	timer_freq = SCTR_FREQ_HZ;
	return 0;
}

u32 timer_freq_hz(void)
{
	return timer_freq;
}

u64 timer_counter(void)
{
	return read_cntpct();
}

/* ---- 时间线记录区（链接脚本 .timelog NOLOAD 段规划，见 justiceBL.ld）---- */
struct timelog_entry {
	u64 stamp;
	const char *desc;
};

extern char __timelog_start[];
extern char __timelog_end[];

#define TIMELOG_CAP		((u32)(__timelog_end - __timelog_start) / \
				 sizeof(struct timelog_entry))

static struct timelog_entry *const log =
	(struct timelog_entry *)__timelog_start;
static u32 log_wr;
static u32 log_dropped;

void record_time(const char *desc)
{
	u64 now = timer_counter();

	if (log_wr < TIMELOG_CAP) {
		log[log_wr].stamp = now;
		log[log_wr].desc = desc;
		log_wr++;
	} else {
		log_dropped++;
	}
}

u32 timelog_count(void)
{
	return log_wr;
}

void record_time_echo(const char *desc)
{
	record_time(desc);
	uart_puts("[t] ");
	uart_puts(desc);
	uart_putc('\r');
	uart_putc('\n');
}

u32 timelog_dropped(void)
{
	return log_dropped;
}

/** 计数 -> 微秒（分解两段避免 stamp*1e6 在长时段溢出 64 位）*/
static u64 counter_to_us(u64 stamp)
{
	u32 rem;
	u64 secs = udiv64_mod(stamp, timer_freq, &rem);

	return secs * 1000000ULL + udiv64_mod((u64)rem * 1000000ULL,
					      timer_freq, NULL);
}

/** us -> 自适应单位串："526us" / "52.6ms" / "12.84s"（列宽交给 %s 修饰）*/
static void fmt_us(char *buf, u32 size, u64 us)
{
	u32 rem, whole;

	if (us < 1000) {
		snprintf(buf, size, "%uus", (u32)us);
	} else if (us < 1000000ULL) {
		whole = udiv64_mod(us, 1000, &rem);
		/* 商和余数必须先落变量：直接把两个表达式塞实参表，
		 * 求值顺序不定（gcc 右到左），rem/100 会先于 rem 赋值 */
		snprintf(buf, size, "%u.%ums", whole, rem / 100);
	} else {
		whole = udiv64_mod(us, 1000000, &rem);
		snprintf(buf, size, "%u.%02us", whole, rem / 10000);
	}
}

void timelog_dump(void)
{
	u64 prev_us = 0;
	char a[16], d[16];

	printf("\ntimeline ctr=%uHz, records=%u, dropped=%u\n",
	       timer_freq, log_wr, log_dropped);
	printf("%3s  %10s  %10s  %s\n", "#", "absolute", "delta", "desc");
	for (u32 i = 0; i < log_wr; i++) {
		u64 us = counter_to_us(log[i].stamp);

		fmt_us(a, sizeof(a), us);
		fmt_us(d, sizeof(d), us - prev_us);
		printf("%3u  %10s  %10s  %s\n", i, a, d, log[i].desc);
		prev_us = us;
	}
	if (log_wr) {
		fmt_us(a, sizeof(a), prev_us);
		printf("total %s\n", a);
	}
}
