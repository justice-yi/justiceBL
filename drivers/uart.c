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

/* 位段（寄存器地址在 imx6ull.h 词典，RM 名） */
#define UCR1_UARTEN		BIT(0)
#define UCR2_SRST		BIT(0)		/* 低有效：0=复位中 1=完成 */
#define UCR2_RXEN		BIT(1)
#define UCR2_TXEN		BIT(2)
#define UCR2_WS			BIT(5)		/* 1 = 8 数据位 */
#define UCR2_IRTS		BIT(14)		/* 忽略 RTS，无流控 */
#define UCR3_RXDMUXSEL		BIT(2)		/* 6ULL UART 为 muxed 模型，必设 */
#define USR2_TXDC		BIT(3)		/* 发送器空 */
#define UFCR_RFDIV_1		(5 << 7)	/* 参考时钟 1 分频 */

#define UART_PAD_CONFIG		0x1B0B1		/* 设备树 pinctrl 原值 */

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
	writel(readl(CCM_CCGR5) | (3 << 24), CCM_CCGR5);

	writel(0, IOMUXC_SW_MUX_CTL_PAD_UART1_TX_DATA);
	writel(UART_PAD_CONFIG, IOMUXC_SW_PAD_CTL_PAD_UART1_TX_DATA);
	writel(0, IOMUXC_SW_MUX_CTL_PAD_UART1_RX_DATA);
	writel(UART_PAD_CONFIG, IOMUXC_SW_PAD_CTL_PAD_UART1_RX_DATA);

	/* 软复位：UCR2 写 0 触发（SRST 低有效），等硬件自置 1 = 完成 */
	writel(0, UART1_UCR1);
	writel(0, UART1_UCR2);
	while (!(readl(UART1_UCR2) & UCR2_SRST)) {
		;
	}

	writel(UFCR_RFDIV_1, UART1_UFCR);
	writel(71, UART1_UBIR);
	writel(3124, UART1_UBMR);

	writel(UCR2_IRTS | UCR2_WS | UCR2_TXEN | UCR2_RXEN | UCR2_SRST,
	       UART1_UCR2);
	writel(readl(UART1_UCR3) | UCR3_RXDMUXSEL, UART1_UCR3);
	writel(UCR1_UARTEN, UART1_UCR1);

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

	while (!(readl(UART1_USR2) & USR2_TXDC) && --t) {
		;
	}
	if (!t) {
		uart_tx_timeouts++;
	}
	writel((u8)c, UART1_UTXD);
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
