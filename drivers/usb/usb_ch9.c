// SPDX-License-Identifier: MIT
/*
 * USB 2.0 第 9 章纯逻辑层 —— 描述符数据 + 标准请求解码。
 *
 * 耦合边界（为什么这个文件里没有任何寄存器）：
 *   描述符内容和请求解码是"协议知识"，硬件引擎（usbd.c）是"搬运知识"。
 *   两者分开后，换 USB IP、换 SoC、改设备身份，互不牵连。
 *   身份信息（VID/PID/名字）唯一来源是 include/usb_config.h。
 */

#include <types.h>
#include <usb_config.h>
#include "usb_ch9.h"

/* ---- 静态描述符：字段逐项来自 include/usb_config.h ---- */

/* 设备描述符：class=0x00 表示"类别在接口层定义"（我们是 vendor 接口） */
static const struct usb_dev_desc dev_desc = {
	.bLength		= 18,
	.bDescriptorType	= USB_DESC_DEVICE,
	.bcdUSB			= 0x0200,	/* USB 2.0 */
	.bDeviceClass		= 0x00,
	.bDeviceSubClass	= 0x00,
	.bDeviceProtocol	= 0x00,
	.bMaxPacketSize0	= 64,		/* ep0 最大包；HS/FS 下 64 均合法 */
	.idVendor		= USB_VENDOR_ID,
	.idProduct		= USB_PRODUCT_ID,
	.bcdDevice		= 0x0100,
	.iManufacturer		= 1,
	.iProduct		= 2,
	.iSerialNumber		= 0,		/* 无序列号 */
	.bNumConfigurations	= 1,
};

/* 接口：vendor 自定义类 0xFF；两个 bulk 端点为将来烧录协议预留 */
static const struct usb_intf_desc intf_desc = {
	.bLength		= 9,
	.bDescriptorType	= 0x04,
	.bInterfaceNumber	= 0,
	.bAlternateSetting	= 0,
	.bNumEndpoints		= 2,
	.bInterfaceClass	= 0xFF,
	.bInterfaceSubClass	= 0x00,
	.bInterfaceProtocol	= 0x00,
	.iInterface		= 0,
};

/* 端点：EP1 OUT(0x01) / EP1 IN(0x81)，bulk，HS 下 512 字节 */
static const struct usb_ep_desc ep_out_desc = {
	.bLength		= 7,
	.bDescriptorType	= 0x05,
	.bEndpointAddress	= (USB_BULK_EP & 0x0F),
	.bmAttributes		= 0x02,
	.wMaxPacketSize		= 512,
	.bInterval		= 0,
};

static const struct usb_ep_desc ep_in_desc = {
	.bLength		= 7,
	.bDescriptorType	= 0x05,
	.bEndpointAddress	= 0x80 | (USB_BULK_EP & 0x0F),
	.bmAttributes		= 0x02,
	.wMaxPacketSize		= 512,
	.bInterval		= 0,
};

/* 配置描述符 = 头(9) + 接口(9) + 端点 OUT(7) + 端点 IN(7) 连续排布 */
static const struct {
	struct usb_cfg_desc	cfg;
	struct usb_intf_desc	intf;
	struct usb_ep_desc	ep_out;
	struct usb_ep_desc	ep_in;
} cfg_blob = {
	.cfg = {
		.bLength		= 9,
		.bDescriptorType	= USB_DESC_CONFIG,
		.wTotalLength		= sizeof(cfg_blob),
		.bNumInterfaces		= 1,
		.bConfigurationValue	= 1,
		.iConfiguration		= 0,
		.bmAttributes		= 0x80,	/* bit7=1 总线供电，bit6=0 非自供电 */
		.bMaxPower		= 50,	/* 100mA（2mA 单位） */
	},
	.intf	= intf_desc,
	.ep_out	= ep_out_desc,
	.ep_in	= ep_in_desc,
};

/* 高速设备必须能应答 device_qualifier（否则规范上应 STALL，主动给出更友好） */
static const struct usb_dev_desc qual_desc = {
	.bLength		= 10,		/* qualifier 比设备描述符短 */
	.bDescriptorType	= USB_DESC_QUALIFIER,
	.bcdUSB			= 0x0200,
	.bDeviceClass		= 0x00,
	.bDeviceSubClass	= 0x00,
	.bDeviceProtocol	= 0x00,
	.bMaxPacketSize0	= 64,
	.bNumConfigurations	= 1,
};

/* ---- 字符串描述符：ascii 名字 -> UTF-16LE，init 时转换一次 ---- */

static u8 langid_desc[4];					/* 语言 ID 表 */
static u8 mfr_desc[2 + 2 * sizeof(USB_MANUFACTURER)];		/* 厂商名 */
static u8 product_desc[2 + 2 * sizeof(USB_PRODUCT)];		/* 产品名=Windows 显示的名字 */
static u8 ms_os_string[18];					/* WCID：MS OS 字符串(0xEE) */
static const u8 compat_id_desc[40] = {				/* WCID：Extended Compatible ID */
/* dwLength=40 */	0x28, 0x00, 0x00, 0x00,
/* bcdVersion 1.0 */	0x00, 0x01,
/* wIndex=4 */		0x04, 0x00,
/* bCount=1 */		0x01,
/* reserved[7] */	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* 首接口 0 */		0x00,
/* reserved */		0x00,
/* compatibleID */	'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,
/* subCompatibleID */	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
/* reserved[6] */	0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static u8 configured;	/* SET_CONFIGURATION 的语义结果（0=未配置） */

/* 把 C 字符串转成 USB 字符串描述符（bLength, bDescriptorType=3, UTF16LE...） */
static void str_to_desc(u8 *dst, const char *src)
{
	u32 n = 0;

	dst[1] = 0x03;					/* bDescriptorType = STRING */
	while (src[n] && n < 126) {
		dst[2 + n * 2] = src[n];		/* 低字节：ascii */
		dst[3 + n * 2] = 0;			/* 高字节：0 */
		n++;
	}
	dst[0] = (u8)(2 + n * 2);			/* bLength */
}

void usb_ch9_init(void)
{
	langid_desc[0] = 4;
	langid_desc[1] = 0x03;
	langid_desc[2] = (u8)(USB_LANGID & 0xFF);
	langid_desc[3] = (u8)(USB_LANGID >> 8);
	str_to_desc(mfr_desc, USB_MANUFACTURER);
	str_to_desc(product_desc, USB_PRODUCT);

	/* WCID（微软 OS 1.0 描述符）：Windows 枚举时固定探测字符串 0xEE，
	 * 签名 MSFT100 + bMS_VendorCode 指明后续厂商请求号；回兼容 ID
	 * "WINUSB" -> winusb.sys 自动绑定，pyusb/libusb 直连零驱动 */
	ms_os_string[0] = 18;
	ms_os_string[1] = 0x03;
	ms_os_string[2] = 'M';  ms_os_string[3] = 0;
	ms_os_string[4] = 'S';  ms_os_string[5] = 0;
	ms_os_string[6] = 'F';  ms_os_string[7] = 0;
	ms_os_string[8] = 'T';  ms_os_string[9] = 0;
	ms_os_string[10] = '1'; ms_os_string[11] = 0;
	ms_os_string[12] = '0'; ms_os_string[13] = 0;
	ms_os_string[14] = '0'; ms_os_string[15] = 0;
	ms_os_string[16] = USB_MS_VENDOR_CODE;
	ms_os_string[17] = 0x00;
}

u8 usb_ch9_configured(void)
{
	return configured;
}

/*
 * 标准请求解码 —— 纯查表/判断，无副作用。
 * SET_ADDRESS 的副作用（写 DEVICEADDR）与 SET_CONFIGURATION 的状态存储
 * 由引擎根据 bRequest 自行处理，见 usbd.c。
 */
void usb_ch9_handle_setup(const struct usb_setup *s, struct usb_ch9_reply *r)
{
	r->data = 0;
	r->len = 0;

	/* 厂商请求（WCID）：Windows 拿 bMS_VendorCode 要 Compatible ID 描述符
	 * （wIndex=4）。只应答设备->主机的读方向，其余厂商请求 stall */
	if ((s->bmRequestType & 0x60) == 0x40) {
		if (s->bmRequestType == 0xC0 &&
		    s->bRequest == USB_MS_VENDOR_CODE && s->wIndex == 4) {
			r->action = USB_CH9_DATA_IN;
			r->data = compat_id_desc;
			r->len = s->wLength < sizeof(compat_id_desc) ?
				 s->wLength : sizeof(compat_id_desc);
		} else {
			r->action = USB_CH9_STALL;
		}
		return;
	}

	switch (s->bRequest) {
	case USB_REQ_GET_DESCRIPTOR: {
		u8 type = s->wValue >> 8;
		u8 idx = s->wValue & 0xFF;
		u16 cap = s->wLength;			/* 主机愿意收多少 */

		switch (type) {
		case USB_DESC_DEVICE:
			r->action = USB_CH9_DATA_IN;
			r->data = &dev_desc;
			r->len = cap < sizeof(dev_desc) ? cap : sizeof(dev_desc);
			return;
		case USB_DESC_CONFIG:
			r->action = USB_CH9_DATA_IN;
			r->data = &cfg_blob;
			r->len = cap < sizeof(cfg_blob) ? cap : sizeof(cfg_blob);
			return;
		case USB_DESC_QUALIFIER:
			r->action = USB_CH9_DATA_IN;
			r->data = &qual_desc;
			r->len = cap < sizeof(qual_desc) ? cap : sizeof(qual_desc);
			return;
		case USB_DESC_STRING:
			r->action = USB_CH9_DATA_IN;
			switch (idx) {
			case 0: r->data = langid_desc; break;
			case 1: r->data = mfr_desc; break;
			case 2: r->data = product_desc; break;
			case 0xEE: r->data = ms_os_string; break;	/* WCID 探测 */
			default:
				r->action = USB_CH9_STALL;	/* 不存在的串号 */
				return;
			}
			r->len = ((const u8 *)r->data)[0];
			r->len = cap < r->len ? cap : r->len;
			return;
		default:
			r->action = USB_CH9_STALL;
			return;
		}
	}

	case USB_REQ_GET_STATUS:
		/* 设备状态：bit0 自供电=0，bit1 远程唤醒=0 */
		r->action = USB_CH9_DATA_IN;
		r->data = "\0\0";
		r->len = 2;
		return;

	case USB_REQ_SET_ADDRESS:
	case USB_REQ_SET_CONFIGURATION:
	case USB_REQ_SET_INTERFACE:
		/* 无数据阶段，状态阶段回 ZLP 即可 */
		r->action = USB_CH9_STATUS_IN;
		return;

	case USB_REQ_CLEAR_FEATURE:
		/* 主机对 ep0 的 halt 解除（我们 stall 过它之后） */
		if (s->wValue == USB_FEATURE_ENDPOINT)
			r->action = USB_CH9_EP0_CLEAR_HALT;
		else
			r->action = USB_CH9_STATUS_IN;
		return;

	default:
		/* 其余请求一律协议 stall（成对置位，收到下一个 setup 自动清除） */
		r->action = USB_CH9_STALL;
		return;
	}
}
