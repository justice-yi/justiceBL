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

/* 总线复位事件计数器：协议层用"计数变了"感知复位、自愈状态机 */
u32 eng_reset_count(void);

#endif /* _JUSTICEBL_USB_ENG_H */
