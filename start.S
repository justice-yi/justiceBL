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
	b	prefetch_abort		@ 0x0c 预取异常
	b	data_abort		@ 0x10 数据异常（含对齐/权限/外部错误）
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
	bic	r0, r0, #(1 << 1)	@ ★SCTLR.A=0（显式清，防踩雷）：bit1 是
				 @ 对齐检查使能（U-Boot system.h CR_A），置 1
				 @ 会让任何未对齐访问触发 Data Abort——
				 @ 2026-11-20 曾误当"SCTLR.U 允许不对齐"打开
				 @ 它，直接导致 eMMC EXT_CSD 读挂死 + USB
				 @ 枚举失败两案（树上 CR_A 实锤）。未对齐
				 @ 防护由 Makefile -mno-unaligned-access
				 @ 在编译器侧根治（不生成未对齐指令）
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

/* ---------- 异常处理器：打印诊断后挂死（无声死→有声死） ----------
 * 模式切换序（ARMv7 异常模型）：
 *   入口处于 ABT 模式（LR/SPSR 为 banked 寄存器），SP_abt 未初始化
 *   → 先用共享寄存器 r0-r3 抢救现场（ fault PC、SPSR、DFAR/DFSR ）
 *   → cps 切回 SVC 模式（栈可用）→ push lr_svc → bl C 函数打印 → 挂死
 * offset 修正：Data Abort 的 LR = 出错 PC + 8；Prefetch = +4（ARMv7 ARM B1.8.3） */

data_abort:
	sub	r0, lr, #8			@ r0 = 出错指令地址
	mrs	r1, spsr			@ r1 = CPSR（出错时的处理器状态）
	mrc	p15, 0, r2, c6, c0, 0		@ r2 = DFAR（出错访问的地址）
	mrc	p15, 0, r3, c5, c0, 0		@ r3 = DFSR（故障类型编码）
	cps	#0x13				@ 切回 SVC 模式（正常栈可用）
	push	{lr}
	bl	data_abort_dump
	b	hang				@ C 函数不返回也兜底

prefetch_abort:
	sub	r0, lr, #4			@ r0 = 取指失败地址
	mrs	r1, spsr			@ r1 = CPSR
	mrc	p15, 0, r2, c6, c0, 2		@ r2 = IFAR（取指失败地址）
	mrc	p15, 0, r3, c5, c0, 1		@ r3 = IFSR（故障类型）
	cps	#0x13
	push	{lr}
	bl	prefetch_abort_dump
	b	hang
