/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_USB_CONFIG_H
#define _JUSTICEBL_USB_CONFIG_H

/*
 * USB 下载模式的设备身份 —— 用户唯一需要改的文件。
 *
 * 改名字/VID/PID 只动这里，usb_ch9.c（描述符）与 usbd.c（引擎）
 * 都从本文件取值，不出现第二份硬编码。
 */

/* VID/PID：设备身份。VID 用 pid.codes 测试段，量产前换成自己的。
 * PID 曾为 0x0001：Windows 对每个 VID/PID 缓存 MS OS 描述符的探测结果，
 * 旧 PID 曾 STALL 过 0xEE 探测（无 WCID 时代），换 0x0002 强制重探测 */
#define USB_VENDOR_ID		0x1209
#define USB_PRODUCT_ID		0x0002

/* MS OS 描述符（WCID）厂商请求号：Windows 拿它要 Compatible ID，
 * 回 "WINUSB" 则自动绑定 winusb.sys（零驱动安装） */
#define USB_MS_VENDOR_CODE	0x42

/* 设备名字（Windows 设备管理器显示的就是 USB_PRODUCT） */
#define USB_MANUFACTURER	"justice"
#define USB_PRODUCT		"justiceBL Loader"

/* 字符串描述符语言（0x0409 = 英语美国） */
#define USB_LANGID		0x0409

/* 供应商自定义接口的 bulk 端点号（为将来 USB 烧录协议预留） */
#define USB_BULK_EP		1

/* 强制全速枚举（PORTSC.PFSC）。0=高速(480M，默认)：烧录带宽 8 倍。
 * 曾在排查断电根因时临时置 1（当时的电流瞬态假设已被否证——根因是
 * 核心板 WDOG_ANY→UM805RE 断电链，见 docs/usb-download-mode.md）；
 * 若 HS 出现枚举不稳可置 1 回退 */
#define USB_FORCE_FULL_SPEED	0

#endif /* _JUSTICEBL_USB_CONFIG_H */
