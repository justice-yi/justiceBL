/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_USB_CH9_H
#define _JUSTICEBL_USB_CH9_H

/*
 * USB 2.0 第 9 章（设备框架）纯逻辑层 —— 描述符结构 + 标准请求解码。
 *
 * 耦合规则：本模块（usb_ch9.c）不包含任何硬件知识——不碰寄存器、
 * 不知道 SNVS/ChipIdea 的存在。它只回答一个问题：
 *     "收到这样一份 setup 包，设备应该回什么数据？"
 * 硬件动作（prime 端点、写寄存器）全部由 usbd.c 引擎完成。
 */

#include <types.h>

/* ---- 标准请求（USB 2.0 spec Table 9-4） ---- */
#define USB_REQ_GET_STATUS		0x00
#define USB_REQ_CLEAR_FEATURE		0x01
#define USB_REQ_SET_FEATURE		0x03
#define USB_REQ_SET_ADDRESS		0x05
#define USB_REQ_GET_DESCRIPTOR		0x06
#define USB_REQ_SET_DESCRIPTOR		0x07
#define USB_REQ_GET_CONFIGURATION	0x08
#define USB_REQ_SET_CONFIGURATION	0x09
#define USB_REQ_SET_INTERFACE		0x0B

#define USB_DESC_DEVICE			0x01
#define USB_DESC_CONFIG			0x02
#define USB_DESC_STRING			0x03
#define USB_DESC_QUALIFIER		0x06

#define USB_FEATURE_ENDPOINT		0x00

/* ---- 描述符结构（packed：字节布局必须与 USB 规范逐字节一致） ---- */

/* 8 字节 setup 包（主机 -> 设备，控制传输的 setup 阶段） */
struct usb_setup {
	u8	bmRequestType;
	u8	bRequest;
	u16	wValue;
	u16	wIndex;
	u16	wLength;
} __attribute__((packed));

/* 设备描述符（18 字节） */
struct usb_dev_desc {
	u8	bLength;
	u8	bDescriptorType;
	u16	bcdUSB;
	u8	bDeviceClass;
	u8	bDeviceSubClass;
	u8	bDeviceProtocol;
	u8	bMaxPacketSize0;
	u16	idVendor;
	u16	idProduct;
	u16	bcdDevice;
	u8	iManufacturer;
	u8	iProduct;
	u8	iSerialNumber;
	u8	bNumConfigurations;
} __attribute__((packed));

/* 配置描述符（9 字节头） */
struct usb_cfg_desc {
	u8	bLength;
	u8	bDescriptorType;
	u16	wTotalLength;
	u8	bNumInterfaces;
	u8	bConfigurationValue;
	u8	iConfiguration;
	u8	bmAttributes;
	u8	bMaxPower;
} __attribute__((packed));

/* 接口描述符（9 字节） */
struct usb_intf_desc {
	u8	bLength;
	u8	bDescriptorType;
	u8	bInterfaceNumber;
	u8	bAlternateSetting;
	u8	bNumEndpoints;
	u8	bInterfaceClass;
	u8	bInterfaceSubClass;
	u8	bInterfaceProtocol;
	u8	iInterface;
} __attribute__((packed));

/* 端点描述符（7 字节） */
struct usb_ep_desc {
	u8	bLength;
	u8	bDescriptorType;
	u8	bEndpointAddress;	/* bit7: 1=IN, 0=OUT; [3:0] 端点号 */
	u8	bmAttributes;
	u16	wMaxPacketSize;
	u8	bInterval;
} __attribute__((packed));

/* ---- 标准请求的应答动作（引擎据此走不同的数据/状态阶段） ---- */
enum usb_ch9_action {
	USB_CH9_DATA_IN,	/* 数据阶段：主机读，回 reply.data/len */
	USB_CH9_STATUS_IN,	/* 无数据阶段：状态阶段回 ZLP（IN 方向） */
	USB_CH9_EP0_CLEAR_HALT,	/* 主机要求解除 ep0 stall：引擎清 ENDPTCTRL0 */
	USB_CH9_STALL,		/* 不认识/不支持：协议 stall（两个方向成对） */
};

struct usb_ch9_reply {
	enum usb_ch9_action	action;
	const void		*data;	/* USB_CH9_DATA_IN 时的数据源 */
	u16			len;
};

/* 构造描述符与字符串（上电一次；把 ascii 名字转成 UTF-16LE 描述符） */
void usb_ch9_init(void);

/* 解码 setup 包 -> 应答动作。纯函数：不碰硬件、无副作用 */
void usb_ch9_handle_setup(const struct usb_setup *s, struct usb_ch9_reply *r);

/* 引擎查询当前配置值（SET_CONFIGURATION 语义上的结果） */
u8 usb_ch9_configured(void);

#endif /* _JUSTICEBL_USB_CH9_H */
