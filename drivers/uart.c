// SPDX-License-Identifier: MIT
/*
 * justiceBL UART1 驱动 —— i.MX UART IP，轮询发送。
 *
 * 初始化序列依据：
 *   - RM UART 章节 / 正点原子《I.MX6U 开发指南》21 章（分频值 71/3124@80MHz）
 *   - U-Boot drivers/serial/serial_mxc.c（SRST 低有效、IRTS、TXDC、RXDMUXSEL）
 * 引脚与时钟门控依据：
 *   - kernel dts imx6ull-alientek-emmc.dts:610（pad 0x1b0b1）
 *   - kernel drivers/clk/imx/clk-imx6ul.c:435（uart1 门控 = CCGR5 bit25:24）
 */

#include <types.h>
#include <imx6ull.h>
#include <uart.h>

/* i.MX UART 寄存器偏移 */
#define UTXD_OFF		0x40
#define UCR1_OFF		0x80
#define UCR2_OFF		0x84
#define UCR3_OFF		0x88
#define UFCR_OFF		0x90
#define USR2_OFF		0x98
#define UBIR_OFF		0xA4
#define UBMR_OFF		0xA8
#define UART_REG(off)		(MX6ULL_UART1_BASE + (off))

/* 常用位 */
#define UCR1_UARTEN		BIT(0)
#define UCR2_SRST		BIT(0)		/* 低有效：0=复位中 1=完成 */
#define UCR2_RXEN		BIT(1)
#define UCR2_TXEN		BIT(2)
#define UCR2_WS			BIT(5)		/* 1 = 8 数据位 */
#define UCR2_IRTS		BIT(14)		/* 忽略 RTS，无流控 */
#define UCR3_RXDMUXSEL		BIT(2)		/* 6ULL UART 为 muxed 模型，必设 */
#define USR2_TXDC		BIT(3)		/* 发送器空 */
#define UFCR_RFDIV_1		(5 << 7)	/* 参考时钟 1 分频 */

/* IOMUX：UART1_TX/RX 复用 ALT0（偏移与 imx6ul-pinfunc.h 同源） */
#define MUX_UART1_TX_OFF	0x84
#define MUX_UART1_RX_OFF	0x88
#define PAD_UART1_TX_OFF	0x310
#define PAD_UART1_RX_OFF	0x314
#define IOMUXC_REG(off)		(MX6ULL_IOMUXC_BASE + (off))
#define UART_PAD_CONFIG		0x1B0B1		/* 设备树 pinctrl 原值 */

/* CCM：uart1 时钟门控 CCGR5 CG12(bit25:24) */
#define CCGR5_REG		(MX6ULL_CCM_BASE + 0x7C)

u32 uart_tx_timeouts;

/**
 * uart_init() - 初始化 UART1 为 115200 8N1（轮询发送）
 *
 * 波特率：参考时钟 80MHz = PLL3(480MHz)/6（复位默认值，第 4 级才接管时钟树），
 * RFDIV=÷1、UBIR=71、UBMR=3124 → 80M/(16×3125/72) = 115200 整除无误差。
 *
 * Return: 0（当前无失败路径，保留 int 以备未来参数校验）
 */
int uart_init(void)
{
	writel(readl(CCGR5_REG) | (3 << 24), CCGR5_REG);

	writel(0, IOMUXC_REG(MUX_UART1_TX_OFF));
	writel(UART_PAD_CONFIG, IOMUXC_REG(PAD_UART1_TX_OFF));
	writel(0, IOMUXC_REG(MUX_UART1_RX_OFF));
	writel(UART_PAD_CONFIG, IOMUXC_REG(PAD_UART1_RX_OFF));

	/* 软复位：UCR2 写 0 触发（SRST 低有效），等硬件自置 1 = 完成 */
	writel(0, UART_REG(UCR1_OFF));
	writel(0, UART_REG(UCR2_OFF));
	while (!(readl(UART_REG(UCR2_OFF)) & UCR2_SRST)) {
		;
	}

	writel(UFCR_RFDIV_1, UART_REG(UFCR_OFF));
	writel(71, UART_REG(UBIR_OFF));
	writel(3124, UART_REG(UBMR_OFF));

	writel(UCR2_IRTS | UCR2_WS | UCR2_TXEN | UCR2_RXEN | UCR2_SRST,
	       UART_REG(UCR2_OFF));
	writel(readl(UART_REG(UCR3_OFF)) | UCR3_RXDMUXSEL, UART_REG(UCR3_OFF));
	writel(UCR1_UARTEN, UART_REG(UCR1_OFF));

	return 0;
}

/**
 * uart_putc() - 发送一个字符（带超时上限的轮询，永不挂死）
 * @c: 待发送字符
 *
 * 等 USR2.TXDC（发送器空）；若 500 万次循环仍未等到则放弃等待强行写入，
 * 并累加 uart_tx_timeouts 供应用层诊断。
 */
void uart_putc(char c)
{
	u32 t = 5000000;

	while (!(readl(UART_REG(USR2_OFF)) & USR2_TXDC) && --t) {
		;
	}
	if (!t) {
		uart_tx_timeouts++;
	}
	writel((u8)c, UART_REG(UTXD_OFF));
}

/**
 * uart_puts() - 发送以 NUL 结尾的字符串，'\n' 前自动补 '\r'
 * @s: 字符串
 */
void uart_puts(const char *s)
{
	while (*s) {
		if (*s == '\n') {
			uart_putc('\r');
		}
		uart_putc(*s++);
	}
}

/**
 * uart_puthex() - 以 0x 前缀的 8 位十六进制发送一个 u32
 * @val: 数值
 */
void uart_puthex(u32 val)
{
	static const char hex[] = "0123456789abcdef";
	int i;

	uart_puts("0x");
	for (i = 28; i >= 0; i -= 4) {
		uart_putc(hex[(val >> i) & 0xf]);
	}
}
