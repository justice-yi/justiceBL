/* SPDX-License-Identifier: MIT */
/*
 * justiceBL 第 0 级：最小启动骨架（纯汇编，无 C）
 *
 * 职责：接住 ROM（或 SDP JUMP）交过来的 CPU，把执行环境收拾干净：
 *   进 SVC 模式 → 指好异常向量表 → 关 MMU/cache（ROM 启动期间可能开着，
 *   RM 8.4.4）→ 设栈 → 清 bss → 待机。后续各级在这之后加代码。
 */

	.arch armv7-a
	.arm

/* ---------- 异常向量表：必须位于镜像最前面（见链接脚本 .vectors） ---------- */

	.section .vectors, "ax"
	.global _start
_start:
	b	reset			@ 0x00 复位：ROM/SDP 跳入的入口
	b	hang			@ 0x04 未定义指令
	b	hang			@ 0x08 软中断（svc）
	b	hang			@ 0x0c 预取异常
	b	hang			@ 0x10 数据异常
	b	hang			@ 0x14 保留
	b	hang			@ 0x18 IRQ
	b	hang			@ 0x1c FIQ

/* ---------- 启动路径 ---------- */

	.section .text
	.global reset
reset:
	/* 1. SVC 模式 + 关 IRQ/FIQ。ROM 交棒时的模式不保证，自己设一遍 */
	cpsid	if
	msr	cpsr_c, #0xd3

	/* 2. VBAR 指向向量表：异常发生时 CPU 自动跳到对应槽位（ARMv7 特性，
	      不设的话异常向量仍指向 0x00000000 的 ROM 区） */
	ldr	r0, =_start
	mcr	p15, 0, r0, c12, c0, 0

	/* 3. 关 I-cache/D-cache/MMU。RM 8.4.4：ROM 启动过程中开着它们，
	      一级镜像拿到 CPU 时必须假定是开的，自己负责关掉 */
	mrc	p15, 0, r0, c1, c0, 0
	bic	r0, r0, #(1 << 12)	@ I-cache
	bic	r0, r0, #(1 << 2)	@ D-cache
	bic	r0, r0, #(1 << 0)	@ MMU
	mcr	p15, 0, r0, c1, c0, 0
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0	@ 使整个 I-cache 失效（关≠空）

	/* 3b. 使能 FPU/NEON 协处理器访问（CPACR CP10/CP11 全权）。
	      复位默认禁用，而 gcc 会把 u64 局部分配到 VFP 寄存器（板上
	      实锤：timelog_dump 序言 vpush {d8} 直接触发未定义指令异常
	      落进 hang——v0.10 卡 timeline 前的根因）。U-Boot/内核同款 */
	mrc	p15, 0, r0, c1, c0, 2
	orr	r0, r0, #(0xF << 20)
	mcr	p15, 0, r0, c1, c0, 2
	isb
	/* 3c. VFP 第二重使能：FPEXC.EN（引擎总开关）。CPACR 只是访问权，
	      EN=0 时 VFP 指令仍触发未定义异常（v0.10.1 板上实锤：只开
	      CPACR 依旧卡 vpush）。Linux 靠 trap 惰性使能，裸机直接开 */
	mov	r0, #(1 << 30)
	vmsr	fpexc, r0

	/* 4. 设栈：SP 指向链接脚本保留的栈区顶端，向下生长 */
	ldr	sp, =_stack_top

	/* 5. 清 .bss。现在还没有变量，骨架先立好，将来加 C 不用改这里 */
	ldr	r0, =__bss_start
	ldr	r1, =__bss_end
	mov	r2, #0
1:	cmp	r0, r1
	strlo	r2, [r0], #4
	blo	1b

	/* 6. 栈 + bss 就绪 = C 运行环境齐了，进 main（不返回）*/
	bl	main
	b	hang			@ main 不应返回；万一返回也趴窝

hang:
	wfi
	b	hang
