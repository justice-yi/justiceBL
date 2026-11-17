/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_USB_ENG_H
#define _JUSTICEBL_USB_ENG_H

/*
 * USB 引擎接口 —— usbd.c（引擎）实现，download.c（协议层）消费。
 * 仅供 drivers/usb/ 内部使用，不进 include/（外部只见 usbd.h）。
 * 命名规矩见 SDK 根 AGENTS.md：动词开头、wait=阻塞/poll=非阻塞、
 * 方向一律 dir_in（1=设备→主机）。
 *
 * 数据模型（U-Boot ci_udc 的 usb_ep_queue 同构）：一次端点传输 =
 * 一个 usb_xfer 请求对象。queue 提交（硬件 DMA 开始等数据/发数据），
 * poll 查完成（完成后 actual/err 有效），在途状态由引擎跟踪——
 * 协议层不持影子标志。
 */

#include <types.h>

struct usb_xfer {
	void *buf;		/* 数据缓冲（IN=来源，OUT=落点） */
	u32 len;		/* 请求长度 */
	u32 actual;		/* 实际完成字节（完成后有效） */
	u32 err;		/* dTD 错误位 HALTED/BufferError/TxError
				 * （完成后有效，0=正常） */
};

/* 提交一次端点传输：填工单 dTD → 挂岗位牌 dQH → 按 PRIME 门铃。
 * 返回后硬件自主搬运，CPU 可去干别的。0=已提交 */
int usb_ep_queue(u8 ep, u8 dir_in, struct usb_xfer *xfer);

/* 非阻塞查询：1=完成（xfer.actual/err 已锁存），0=在途或未提交 */
int usb_ep_poll(u8 ep, u8 dir_in, struct usb_xfer *xfer);

/* 阻塞等完成（EP0 控制传输用）：0=完成；1=被新 setup 打断（本次作废，
 * 主机会重发 setup）；-1=超时 */
int usb_ep_wait(u8 ep, u8 dir_in, struct usb_xfer *xfer);

/* EP1（bulk）halt 控制（评审 M2 的 halt 策略）：
 * - clear_unexpected_stall：协议层 poll 的自愈——host 未请求 halt 时清
 *   意外 stall（先 FLUSH 作废在途请求+复位 data toggle 再清位），返回
 *   清除的 stall 位（0=无动作）；host 已请求 halt（halt_set 过）则不动
 * - halt_set(dir_in)：host SET_FEATURE(ENDPOINT_HALT)——只置寻址方向
 *   的 stall（USB 2.0 9.4.5），记软件 halt 标志
 * - halt_clear()：host CLEAR_FEATURE(ENDPOINT_HALT)——解除软件 halt、
 *   FLUSH 复位 toggle、清双向 stall
 * - rx_abort()：作废 EP1 OUT 在途接收请求（协议层写失败弃流时用，
 *   FLUSH 会作废在途工单且不产生完成位） */
u32 usb_ep1_clear_unexpected_stall(void);
void usb_ep1_halt_set(u8 dir_in);
void usb_ep1_halt_clear(void);
void usb_ep1_rx_abort(void);

/* EP1 是否处于 halt（GET_STATUS endpoint 用）：bit0=RXS bit1=TXS */
u32 usb_ep1_halt_status(void);

/* 链路代际计数（总线复位/EP1 flush 均 +1）：协议层用"计数变了"
 * 感知链路态变化、丢弃进行中状态自愈 */
u32 usb_link_generation(void);

#endif /* _JUSTICEBL_USB_ENG_H */
