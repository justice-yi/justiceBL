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
 */

ENTRY(_start)

MEMORY
{
	OCRAM (rwx) : ORIGIN = 0x00908000, LENGTH = 64K
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

	/* 栈：bss 之后保留 8KB，SP 从顶端向下生长 */
	. = ALIGN(8);
	. += 8K;
	_stack_top = .;

	/DISCARD/ : { *(.note*) *(.comment*) }
}
