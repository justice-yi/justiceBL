/*
 * justiceBL 链接脚本
 *
 * 整个镜像落在 OCRAM 空闲区（RM 8.4.1 图 8-3）：
 *   0x00907000 ~ 0x00918000 共 68KB 是 ROM 启动期间留给一级镜像的空区。
 *   取 0x00908000 起、上限 0x00918000（64KB）：与 U-Boot 官方 SPL 的
 *   TEXT_BASE 同基址；0x00918000 之上是 ROM 的 MMU 表(20KB)/栈，不能占。
 *
 * 附带的好处：OCRAM 上电即活——不依赖 DCD 初始化 DDR，justiceBL 的 .imx
 * 头里 DCD 变成可选项（带上也无妨），DDR 坏/未焊也能跑到加载 U-Boot 之前。
 * U-Boot 本体仍加载到 DDR 0x87800000（第 5 级初始化/依赖 DDR 时再处理）。
 *
 * 2026-11-19：DDR 低位划 16M 作大数据区（.ddr_bss，NOLOAD 不进镜像）。
 * 前提链：DCD 已在 ROM 阶段初始化 DDR（mkimx dcd=yes 实锤；U-Boot 本就
 * 被装到 DDR 0x87800000）——代码/栈/时序敏感数据仍留 OCRAM，仅大缓冲
 * 下沉（如 USB 下载乒乓 chunk 32K，OCRAM 64K 挤爆的元凶）。避开
 * 0x87800000 起 U-Boot 落点，取 0x80000000 低 16M。
 */

ENTRY(_start)

MEMORY
{
	OCRAM (rwx) : ORIGIN = 0x00908000, LENGTH = 64K
	DDR   (rwx) : ORIGIN = 0x80000000, LENGTH = 16M
}

SECTIONS
{
	. = ORIGIN(OCRAM);

	/* 向量表必须排第一：异常槽位 = _start + 0x00..0x1c */
	.vectors : { KEEP(*(.vectors)) } > OCRAM

	.text	: { *(.text*) }		> OCRAM
	.rodata	: { *(.rodata*) }	> OCRAM
	.data	: { *(.data*) }		> OCRAM

	.bss (NOLOAD) :
	{
		. = ALIGN(4);		/* str 按 4 字节步进清零，起点必须 4 对齐 */
		__bss_start = .;
		*(.bss*) *(COMMON)
		__bss_end = .;
	} > OCRAM

	/* 时间线记录区（timer.c）：128 条 × 16B = 2KB，NOLOAD 不占镜像。
	 * 容量改这里一处，模块按 __timelog_start/end 自动得出。
	 * （2026-11-17：4K→2K——USB 下载乒乓双缓冲多占 16K bss，调试
	 * 记录让路；128 条对启动时间线仍充裕） */
	.timelog (NOLOAD) :
	{
		. = ALIGN(8);		/* timelog_entry 含 u64，8 字节对齐 */
		__timelog_start = .;
		. += 2K;
		__timelog_end = .;
	} > OCRAM

	/* 栈：bss+timelog 之后保留 8KB，SP 从顶端向下生长。
	 * ★必须在 .ddr_bss 之前定义——链接器的位置计数器跟着最后一个
	 * section 的区域走，如果 .ddr_bss > DDR 在前面，. += 8K 会在
	 * DDR 地址空间推进，栈就跑进 DDR 了（2026-11-19 板上实证：
	 * DFAR=0x80009e19 即 DDR 中的栈地址，SCTLR.U=1 也不救——
	 * DDR 控制器自身可能有对齐约束） */
	. = ALIGN(8);
	. += 8K;
	_stack_top = .;

	/* 大数据缓冲区（NOLOAD 不进镜像）：DDR 低位，避开 U-Boot 落点 */
	.ddr_bss (NOLOAD) :
	{
		. = ALIGN(32);		/* dTD 页指针/DMA 对齐习惯 */
		__ddr_bss_start = .;
		KEEP(*(.ddr_bss))
		__ddr_bss_end = .;
	} > DDR

	/DISCARD/ : { *(.note*) *(.comment*) }
}
