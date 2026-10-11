/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_UART_H
#define _JUSTICEBL_UART_H

/*
 * UART1（调试口 ttymxc0）驱动 API：115200 8N1，轮询发送，无中断。
 */

#include <types.h>

int uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_puthex(u32 val);

/* 发送等待超时计数（诊断用；>0 说明 TXDC 迟迟不来，jiffies 风格全局量） */
extern u32 uart_tx_timeouts;

#endif /* _JUSTICEBL_UART_H */
