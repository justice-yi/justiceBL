// SPDX-License-Identifier: MIT
/*
 * justiceBL 应用层 —— 启动自检心跳 + 串口 banner（第 1+2 级）。
 *
 * 自检灯语（每档 0.3~1 秒，肉眼可辨；串口坏也能看灯诊断）：
 *   ① 慢闪 3 次        = 已进 main、LED 通路 OK
 *   ② 长亮 1 秒        = uart_init 已返回
 *   ④a 快闪 1 下进心跳 = 发送等待全部正常（TXDC 都来了）
 *   ④b 长亮/长灭循环   = 发送超时 ≥1 次（串口硬件没通）
 *   ③ ~1.6Hz 心跳      = 主循环活着，每圈一行 alive
 */

#include <types.h>
#include <uart.h>
#include <led.h>

/* 粗延时循环数：约 0.3s@396MHz。第 4 级 GPT 定时器到位后换 udelay/mdelay */
#define BLINK_DELAY_LOOPS	30000000U
#define LONG_DELAY_LOOPS	100000000U

static void delay_loops(volatile u32 loops)
{
	while (loops--) {
		__asm__ volatile ("nop");
	}
}

int main(void)
{
	unsigned int heartbeat = 0;

	/* ① LED 先行：串口初始化若再出问题，灯先报平安 */
	led_init();
	for (int i = 0; i < 3; i++) {
		led_set(1);
		delay_loops(BLINK_DELAY_LOOPS);
		led_set(0);
		delay_loops(BLINK_DELAY_LOOPS);
	}

	/* ② uart_init 是历史上死过人的地方，给个独立标记 */
	uart_init();
	led_set(1);
	delay_loops(LONG_DELAY_LOOPS);
	led_set(0);

	uart_puts("\njusticeBL v0.4 (modular) @ OCRAM 0x00908000\n");
	uart_puts("UART1 115200 8N1 | LED0 = GPIO1_IO03 active-low\n");

	/* ④ 串口健康灯语 */
	if (uart_tx_timeouts == 0) {
		led_set(1);
		delay_loops(BLINK_DELAY_LOOPS);
		led_set(0);
		delay_loops(BLINK_DELAY_LOOPS);
	} else {
		for (;;) {
			led_set(1);
			delay_loops(LONG_DELAY_LOOPS);
			led_set(0);
			delay_loops(LONG_DELAY_LOOPS);
		}
	}

	/* ③ 主心跳循环 */
	for (;;) {
		led_set(1);
		delay_loops(BLINK_DELAY_LOOPS);
		led_set(0);
		delay_loops(BLINK_DELAY_LOOPS);

		uart_puts("alive #");
		uart_puthex(++heartbeat);
		uart_putc('\n');
	}
}
