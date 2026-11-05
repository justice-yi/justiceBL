/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_TIMER_H
#define _JUSTICEBL_TIMER_H

/*
 * 时间线记录 —— 精确时间戳 + 打点 API。
 *
 * 计时器：ARM 架构系统计数器（CNTPCT，mrrc p15,0,c14），由 SoC 内
 * SP807 System Counter 块供数。计数器**复位默认不运行**，须先写
 * SCTR CNTCR 使能——这就是 v0.10.1 前板上读数恒 0 的根因。
 *
 * 路线依据（树内代码优先）：
 *   - U-Boot arch/arm/mach-imx/syscounter.c：本板现役 U-Boot 即此实现
 *     （output/uboot/.config: SYSCOUNTER_TIMER=y）——使能序 = 写 CNTFRQ
 *     (mcr) + CNTFID0 + CNTCR ENABLE|HDBG|FREQ0，读数用 CNTPCT
 *   - RM 内存表：System Counter_CTRL = 0x021D_C000（另有 CMP/RD 两块）；
 *     时钟图：门控 CCGR5[CG1]（IPG_CLK 供寄存器访问）
 *   - U-Boot include/configs/mx6_common.h: CONFIG_SC_TIMER_CLK = 8000000
 *     （固定 8MHz，与 CCM 时钟树无关——无需运行时算 periph_clk）
 *   - 弃 A9 式 MPCore Global Timer@0x00A00200：板上实测读全 0，
 *     kernel dts 无该节点（imx6ul.dtsi 只有 status="disabled" 的
 *     armv7-timer，Linux 实际走 GPT），U-Boot 亦从不访问。
 *
 * 8MHz → 分辨率 125ns，64 位计数 7 万年不回绕。
 */

#include <types.h>

int timer_init(void);			/* 使能 SCTR + 回读自检（main 最早调）*/
u64 timer_counter(void);		/* 当前 64 位原始计数（CNTPCT 原子读）*/
u32 timer_freq_hz(void);		/* 计数频率（换算基准，现为常量 8MHz）*/

/**
 * record_time() - 打一个时间点
 * @desc: 时间点描述（字符串字面量指针，存 rodata 不拷贝），如 "sd_init in"
 *
 * 记录 {64 位计数, 描述指针} 到 .timelog 区（链接脚本规划，容量按
 * 段大小自动得出）。区满后不再环绕（启动分析早期记录最珍贵），
 * 后续调用只累加丢弃计数。
 */
void record_time(const char *desc);

/**
 * record_time_echo() - 打点并实时回显（诊断用）
 *
 * 与 record_time 相同，但立即向 UART 输出一行 "[t] desc"——
 * 真挂死（到不了 timelog_dump）的场景也能看到最后活着的位置。
 */
void record_time_echo(const char *desc);

/** 当前已记录条数（含满后丢弃计数，见 timelog_dropped）*/
u32 timelog_count(void);
u32 timelog_dropped(void);		/* 容量满后被丢弃的打点数 */

/** UART 输出全表：序号 / 绝对 us / 相邻差 us / 描述 */
void timelog_dump(void);

#endif /* _JUSTICEBL_TIMER_H */
