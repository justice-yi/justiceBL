/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_USB_ENG_H
#define _JUSTICEBL_USB_ENG_H

/*
 * USB 引擎内部接口 —— usbd.c（引擎）实现，usb_dl.c（协议层）消费。
 * 仅供 drivers/usb/ 内部使用，不进 include/（外部只见 usbd.h）。
 */

#include <types.h>

/* prime 一个方向的端点并等硬件就绪（阻塞）；0 成功 */
int eng_ep_prime(u8 ep, u8 tx, const void *buf, u32 len);

/* 阻塞等传输完成：0=完成（已 W1C），1=被新 setup 打断，-1=超时 */
int eng_ep_wait_done(u8 ep, u8 tx);

/* 非阻塞查询：1=完成（已 W1C），0=未完成 */
int eng_ep_check_done(u8 ep, u8 tx);

/* 上一次 OUT 传输实收字节数（dTD 剩余量反推；须在 check_done=1 后读） */
u32 eng_ep_rx_len(u8 ep, u8 tx);

/* 上一次传输的 dTD 错误位（HALTED/BufferError/TxError，0=正常）；
 * check_done=1 后读取，prime 时自动清零 */
u32 eng_last_xfer_err(void);

/* EP1（bulk）stall 控制（评审 M2 的 halt 策略）：
 * - stall_recover：协议层 poll 的自愈——host 未请求 halt 时清意外 stall
 *   （先 FLUSH 复位 data toggle/丢弃挂起 dTD 再清位），返回清除的 stall
 *   位（0=无动作）；host 已请求 halt（set_stall 过）则不动，返回 0
 * - set_stall(dir_in)：host SET_FEATURE(ENDPOINT_HALT)——只置寻址方向
 *   的 stall（USB 2.0 9.4.5），记软件 halt 标志
 * - halt_clear()：host CLEAR_FEATURE(ENDPOINT_HALT)——解除软件 halt、
 *   FLUSH 复位 toggle、清双向 stall */
u32 eng_ep1_stall_recover(void);
void eng_ep1_set_stall(u8 dir_in);
void eng_ep1_halt_clear(void);

/* EP1 是否处于 halt（GET_STATUS endpoint 用）：bit0=RXS bit1=TXS */
u32 eng_ep1_halt_bits(void);

/* 总线复位事件计数器：协议层用"计数变了"感知复位、自愈状态机 */
u32 eng_reset_count(void);

#endif /* _JUSTICEBL_USB_ENG_H */
