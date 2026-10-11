/*
 * justiceBL DDR 变体链接脚本 —— A/B 对照实验用
 *
 * 与 justiceBL.ld 唯一区别：ORIGIN = 0x87800000（官方 U-Boot 的 TEXT_BASE）。
 * 配合 mkimx.py 的 DDR 模式（嵌入官方 DCD）得到与正点原子裸机实验完全同构的
 * 启动配置：ROM 执行 DCD 初始化 DDR → 镜像加载到 0x87800000 → 跳转。
 * 若此变体能启动而 OCRAM 版不能 → 锁定问题在 OCRAM 低段；若都不能 → 拨码/串口。
 */

ENTRY(_start)

MEMORY
{
	DDR (rwx) : ORIGIN = 0x87800000, LENGTH = 1M
}

SECTIONS
{
	. = ORIGIN(DDR);

	.vectors : { KEEP(*(.vectors)) } > DDR

	.text	: { *(.text*) }		> DDR
	.rodata	: { *(.rodata*) }	> DDR
	.data	: { *(.data*) }		> DDR

	.bss (NOLOAD) :
	{
		. = ALIGN(4);
		__bss_start = .;
		*(.bss*) *(COMMON)
		__bss_end = .;
	} > DDR

	. = ALIGN(8);
	. += 8K;
	_stack_top = .;

	/DISCARD/ : { *(.note*) *(.comment*) }
}
