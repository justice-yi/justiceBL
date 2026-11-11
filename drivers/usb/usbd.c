// SPDX-License-Identifier: MIT
/*
 * USB 设备引擎（ChipIdea IP 层）—— EP0 状态机 + 端点数据结构 + 轮询主循环。
 *
 * 耦合规则：
 *   - 本文件不含任何 i.MX 具体地址：控制器基址、时钟、PHY 全部来自
 *     usbd_soc.h 接口（换 SoC = 换 usbd_soc.c 实现）。
 *   - 协议内容（描述符、请求解码）全部来自 usb_ch9.c（换身份/协议不碰引擎）。
 *   - 本文件持有的是 ChipIdea IP 级常量（寄存器偏移、dQH/dTD 布局），
 *     出处为 RM 56.4.5（数据结构）/56.4.6（设备操作模型）/56.6.x（寄存器）。
 *
 * 采样模型：全程轮询（裸机无中断分发），RM 56.4.6.1 第 4 步"使能中断"
 * 在本实现中被"屏蔽中断 + 轮询状态寄存器"替代，语义等价、代码更短。
 *
 * 内存一致性：justiceBL 的 start.S 关闭了 MMU/I-cache/D-cache，
 * CPU 与 USB DMA 看到同一物理内存，无需 cache 维护。
 */

#include <types.h>
#include <printf.h>
#include <usb_config.h>
#include "usb_ch9.h"
#include "usb_eng.h"
#include "usb_dl.h"
#include "usbd_soc.h"
#include <usbd.h>

/* ---- IP 级寄存器偏移（RM 56.6.x，绝对地址 = usbd_soc_core_base() + off） ---- */
#define R_USBCMD		0x140		/* 56.6.18 */
#define R_USBSTS		0x144
#define R_USBINTR		0x148		/* 56.6.19 */
#define R_DEVICEADDR		0x154		/* 56.6.23 */
#define R_ENDPTLISTADDR		0x158		/* 56.6.25 */
#define R_PORTSC1		0x184		/* 56.6.31 */
#define R_OTGSC			0x1A4		/* 56.6.32 */
#define R_USBMODE		0x1A8		/* 56.6.33 */
#define R_ENDPTSETUPSTAT	0x1AC		/* 56.6.34 */
#define R_ENDPTPRIME		0x1B0		/* 56.6.35 */
#define R_ENDPTFLUSH		0x1B4		/* 56.6.36 */
#define R_ENDPTSTAT		0x1B8		/* 56.6.37 */
#define R_ENDPTCOMPLETE		0x1BC		/* 56.6.38 */
#define R_ENDPTCTRL0		0x1C0		/* 56.6.39，每端点 +4 */

/* ---- 位定义 ---- */
/* USBCMD（56.6.18） */
#define USBCMD_RS		BIT(0)		/* Run/Stop：1=跑，D+ 上拉出现 */
#define USBCMD_RST		BIT(1)		/* 控制器软复位，硬件自动清零 */
#define USBCMD_SUTW		BIT(13)		/* Setup TripWire（读 setup 的信号量） */
/* USBSTS（56.6.19，写 1 清零） */
#define USBSTS_UI		BIT(0)		/* USB 中断（setup/完成等） */
#define USBSTS_UEI		BIT(1)		/* USB 错误 */
#define USBSTS_PCI		BIT(2)		/* 端口变化 */
#define USBSTS_URI		BIT(6)		/* 收到总线复位 */
#define USBSTS_W1C_MASK		(USBSTS_UI | USBSTS_UEI | USBSTS_PCI | USBSTS_URI)
/* USBMODE（56.6.33）：位图 SDIS(4) SLOM(3) ES(2) CM[1:0]——CM 在 bit[1:0]！
 * （踩坑：按另一代 IP 的习惯写成 0x2<<2，结果 CM=idle + SLOM 置位，
 *  控制器没进设备模式，D+ 永不上拉——U-Boot ehci-ci.h CM_DEVICE=(2<<0) 实锤） */
#define USBMODE_CM_DEV		0x2		/* CM[1:0]=10 设备模式（OTG 核复位后是 00 idle，必须写） */
#define USBMODE_SLOM		BIT(3)		/* 关 setup 锁存，改用 SUTW tripwire（RM 56.4.6.4.2.1 推荐方案） */
/* DEVICEADDR（56.6.23） */
#define DEVICEADDR_USBADRA	BIT(24)		/* 地址暂存：状态阶段 ZLP 发完才生效（合规范） */
/* ENDPTCTRL(n)（56.6.39）：RXS(0) RXT[3:2] RXE(7) | TXS(16) TXT[19:18] TXE(23) */
#define EPCTRL_RXS		BIT(0)
#define EPCTRL_RXT_CTRL		(0x0 << 2)
#define EPCTRL_RXE		BIT(7)
#define EPCTRL_TXS		BIT(16)
#define EPCTRL_TXT_CTRL		(0x0 << 18)
#define EPCTRL_TXE		BIT(23)
#define EPCTRL_TXT_BULK		(0x2 << 18)
#define EPCTRL_RXT_BULK		(0x2 << 2)
/* prime/flush/complete：RX 位 = 端点号，TX 位 = 16+端点号 */
#define EPBIT(ep, tx)		((tx) ? BIT(16 + (ep)) : BIT(ep))

/* ---- 数据结构（RM 56.4.5） ---- */

/* dQH：有效内容 48 字节，但硬件槽位 = 64 字节（ENDPTLISTADDR+64×下标）——
 * 结构体必须补齐到 64，否则槽 1 起（ep0 TX）全部错位：
 * C 的 qh[1]@+48，硬件读的 ep0-TX dQH 在 +64（踩坑：setup 收发全对、
 * TX prime 全挂，就是这 16 字节错位） */
struct usb_dqh {
	u32	caps;		/* MULT[31:30] ZLT(29) MPL[26:16] IOS(15) */
	u32	curr_dtd;	/* 硬件专用 */
	u32	next_dtd;	/* [31:5] 地址 | T(0) */
	u32	token;		/* TotalBytes[30:16] IOC(15) Status[7:0]（ACTIVE=bit7） */
	u32	buf[5];
	u32	rsv;
	u32	setup[2];	/* setup 包 8 字节落点（仅 RX dQH 有意义） */
	u32	rsv2[4];	/* 补齐到硬件槽位 64 字节 */
} __attribute__((packed));

/* dTD：硬件描述符 32 字节整（next+token+5 页缓冲+1 保留）——
 * next 指针 [31:5] 要求 32 字节对齐，结构体尺寸必须正好 32 */
struct usb_dtd {
	u32	next;		/* [31:5] | T(0) */
	u32	token;		/* 同上 */
	u32	buf[5];
	u32	pad;
} __attribute__((packed));

/* 尺寸护栏：与硬件槽位不一致时直接编译失败 */
__extension__ _Static_assert(sizeof(struct usb_dqh) == 64, "dQH must be 64B (hw slot size)");
__extension__ _Static_assert(sizeof(struct usb_dtd) == 32, "dTD must be 32B");

#define DTD_ACTIVE		BIT(7)
#define DTD_TERMINATE		BIT(0)

/* EP0 大包 64；描述符最长 32+字符串都 ≤64，单 dTD 单包足够（why: 控制传输无大数据） */
#define EP0_MPS			64

/* 端点布置：下标 = ep*2 + (tx?1:0)，RM 图 56-27"偶收奇发" */
#define QH_IDX(ep, tx)		(((ep) * 2) + ((tx) ? 1 : 0))

static struct usb_dqh qh[4] __attribute__((aligned(4096)));	/* ep0 rx/tx + ep1 rx/tx（U-Boot 用 4K 对齐，RM 最低 64） */
static struct usb_dtd dtd[4] __attribute__((aligned(32)));	/* 与 qh 同序的 dTD 池 */

static u8 dev_addr;		/* 当前 USB 设备地址（总线复位归 0） */
static u32 primed_len[4];	/* 各方向 prime 的字节数（rx_len 反推实收用） */
static u32 eng_rst_cnt;		/* 总线复位事件计数（协议层自愈用） */
static u32 eng_xfer_err;	/* 上一次传输的 dTD 错误位（M1） */
static u32 ep1_sw_halt;		/* host SET_FEATURE(ENDPOINT_HALT) 请求的 halt：
				 * 置位期间 poll 自愈不得清除（评审 M2） */

/* 逐字节拷贝（freestanding 无 libc） */
static void xcopy(void *dst, const void *src, u32 n)
{
	u8 *d = dst;
	const u8 *s = src;

	while (n--)
		*d++ = *s++;
}

/* 轮询用超时（~0.25s；主机重试间隔内必须返回，否则表现为冻结） */
#define WAIT_LOOPS	2000000

/* ---- dQH / dTD ---- */

/*
 * 端点能力字（RM Table 56-57）：MPL、IOS、ZLT、MULT。
 * ZLT=0（允许硬件自动补零长包收尾）、MULT=00（非 ISO 端点必须，RM 表注）。
 * EP0 的 IOS=1：收到 setup 时置 UI 位（轮询模式下无所谓，按规范语义保留）。
 */
static void qh_reinit(void)
{
	u32 i;

	for (i = 0; i < 4; i++) {
		u32 mpl = (i < 2) ? EP0_MPS : 512;	/* ep0 控制 64，ep1 bulk 512 */
		u32 ios = (i < 2) ? BIT(15) : 0;

		qh[i].caps = (mpl << 16) | ios | BIT(29);	/* ZLT=1：ZLP 由软件显式发（U-Boot ci_udc 同款） */
		qh[i].next_dtd = DTD_TERMINATE;		/* 无挂起传输 */
		qh[i].token = 0;
		qh[i].setup[0] = 0;
		qh[i].setup[1] = 0;
	}
}

/* 组一个 dTD 并挂到 dQH 上，随后由调用方写 PRIME 位（RM 56.4.6.4.2.2/3） */
int eng_ep_prime(u8 ep, u8 tx, const void *buf, u32 len)
{
	u32 idx = QH_IDX(ep, tx);
	struct usb_dtd *d = &dtd[idx];
	u32 bit = EPBIT(ep, tx);
	u32 to = WAIT_LOOPS;
	u32 p;

	if (len > 0x4000)			/* RM：单 dTD 建议 ≤16K，这里远用不到 */
		return -1;

	/* dTD：终止链（单描述符）、长度、ACTIVE 置位、5 页缓冲指针。
	 * 页指针约定（U-Boot ci_udc:485-489 同款）：page0 = 完整地址（[11:0]
	 * 是硬件用的页内起始偏移，不能丢！），page1-4 = 4K 对齐基址 + 步进 */
	d->next = DTD_TERMINATE;
	d->token = (len << 16) | DTD_ACTIVE;
	p = (u32)buf & 0xFFFFF000u;
	d->buf[0] = (u32)buf;
	for (u32 i = 1; i < 5; i++)
		d->buf[i] = p + (i << 12);

	qh[idx].next_dtd = (u32)d;		/* T=0：指针有效 */
	primed_len[idx] = len;
	eng_xfer_err = 0;
	/* ARM posted write：dTD/dQH 内容必须先于 PRIME 寄存器写到达——
	 * 无 cache 时是防御性屏障，将来开 cache 后是必需（评审 M 建议） */
	__asm__ volatile ("dmb ish" ::: "memory");

	/* 写 PRIME 位；硬件把 dTD 拷进 dQH overlay，位置就绪 */
	usbd_soc_wr32(R_ENDPTPRIME, bit);
	while (usbd_soc_rd32(R_ENDPTPRIME) & bit) {
		if (--to == 0)
			return -1;
	}
	/* RM：PRIME 清零且 ENDPTSTAT 置位 = prime 成功。注意两步之间有硬件
	 * 延迟——STAT 必须轮询等待，读一次会误判失败（2026-11-08 实测踩坑） */
	to = 1000;
	while (!(usbd_soc_rd32(R_ENDPTSTAT) & bit) && --to)
		;
	if (!(usbd_soc_rd32(R_ENDPTSTAT) & bit))
		return -1;
	return 0;
}

/* 等一个端点方向传输完成（ENDPTCOMPLETE 置位），W1C 清掉后返回 0 */
int eng_ep_wait_done(u8 ep, u8 tx)
{
	u32 bit = EPBIT(ep, tx);
	u32 to = WAIT_LOOPS;

	while (!(usbd_soc_rd32(R_ENDPTCOMPLETE) & bit)) {
		/* 期间若主机直接来新 setup，当前传输作废（RM 56.4.6.4.2.2） */
		if (usbd_soc_rd32(R_ENDPTSETUPSTAT) & 1)
			return 1;		/* 1 = 被新 setup 打断，调用方丢弃本次 */
		if (--to == 0)
			return -1;
	}
	usbd_soc_wr32(R_ENDPTCOMPLETE, bit);	/* W1C */
	return 0;
}

/* 非阻塞查询完成位；1=完成（已 W1C） */
int eng_ep_check_done(u8 ep, u8 tx)
{
	u32 bit = EPBIT(ep, tx);

	if (!(usbd_soc_rd32(R_ENDPTCOMPLETE) & bit))
		return 0;
	usbd_soc_wr32(R_ENDPTCOMPLETE, bit);
	/* M1：dTD 错误位（HALTED/BufferError/TxError）——
	 * USB 包级 CRC 由硬件重传保证，这里只捕 babble/buffer 异常 */
	eng_xfer_err = dtd[QH_IDX(ep, tx)].token & 0x7E;
	return 1;
}

/* 上一次传输的 dTD 错误位（0=正常）；prime 时自动清零 */
u32 eng_last_xfer_err(void)
{
	return eng_xfer_err;
}

/* 上一次 OUT 实收字节数 = prime 长度 - dTD 剩余量（check_done=1 后读） */
u32 eng_ep_rx_len(u8 ep, u8 tx)
{
	u32 idx = QH_IDX(ep, tx);

	return primed_len[idx] - ((dtd[idx].token >> 16) & 0x7FFF);
}

/* 总线复位事件计数器：协议层靠"计数变了"自愈状态机 */
u32 eng_reset_count(void)
{
	return eng_rst_cnt;
}

/* EP1 双向 FLUSH：清挂起 dTD 并复位 data toggle（RM 56.4.6.4.4），
 * 有界等待；清 stall 位前必须 flush，否则残留 dTD 与后续 prime 竞态 */
static void ep1_flush(void)
{
	u32 bits = EPBIT(USB_BULK_EP, 0) | EPBIT(USB_BULK_EP, 1);
	u32 to = WAIT_LOOPS;

	usbd_soc_wr32(R_ENDPTFLUSH, bits);
	while ((usbd_soc_rd32(R_ENDPTFLUSH) & bits) && --to)
		;
}

/* 清 EP1 双向 stall 位，返回清除的位 */
static u32 ep1_clear_stall_bits(void)
{
	u32 ctrl = R_ENDPTCTRL0 + 4 * USB_BULK_EP;
	u32 c = usbd_soc_rd32(ctrl);
	u32 hit = c & (EPCTRL_RXS | EPCTRL_TXS);

	if (hit)
		usbd_soc_wr32(ctrl, c & ~hit);
	return hit;
}

/* EP1 stall 自愈：host 请求的 halt（ep1_sw_halt）期间不撤；否则先 FLUSH
 * （复位 data toggle + 丢挂起 dTD）再清 stall 位。返回清除的位（0=无动作），
 * 调用方打印定位 */
u32 eng_ep1_stall_recover(void)
{
	u32 hit;

	if (ep1_sw_halt)
		return 0;
	hit = ep1_clear_stall_bits();
	if (hit)
		ep1_flush();
	return hit;
}

/* host SET_FEATURE(ENDPOINT_HALT)：只置寻址方向的 stall（评审 M1/M2） */
void eng_ep1_set_stall(u8 dir_in)
{
	u32 ctrl = R_ENDPTCTRL0 + 4 * USB_BULK_EP;
	u32 c = usbd_soc_rd32(ctrl);

	ep1_sw_halt = 1;
	usbd_soc_wr32(ctrl, c | (dir_in ? EPCTRL_TXS : EPCTRL_RXS));
}

/* host CLEAR_FEATURE(ENDPOINT_HALT)：解除软件 halt + FLUSH 复位 toggle */
void eng_ep1_halt_clear(void)
{
	ep1_sw_halt = 0;
	ep1_flush();
	ep1_clear_stall_bits();
}

/* EP1 halt 状态（GET_STATUS(endpoint) 应答用）：bit0=RXS bit1=TXS */
u32 eng_ep1_halt_bits(void)
{
	u32 c = usbd_soc_rd32(R_ENDPTCTRL0 + 4 * USB_BULK_EP);
	u32 bits = 0;

	if (c & EPCTRL_RXS)
		bits |= 1;
	if (c & EPCTRL_TXS)
		bits |= 2;
	return bits;
}

/* ---- EP0 处理 ---- */

/* 协议 stall：两方向成对置位（RM 56.4.6.3.2：单次写保证同时刻生效） */
static void ep0_stall(void)
{
	u32 c = usbd_soc_rd32(R_ENDPTCTRL0);

	usbd_soc_wr32(R_ENDPTCTRL0, c | EPCTRL_TXS | EPCTRL_RXS);
}

static void ep0_clear_stall(void)
{
	u32 c = usbd_soc_rd32(R_ENDPTCTRL0);

	usbd_soc_wr32(R_ENDPTCTRL0, c & ~(EPCTRL_TXS | EPCTRL_RXS));
}

/* 状态阶段（IN 方向 ZLP）：total=0 的 dTD，RM 明言硬件执行零长事务 */
static int ep0_status_in(void)
{
	int ret = eng_ep_prime(0, 1, 0, 0);

	if (ret)
		return ret;
	return eng_ep_wait_done(0, 1);
}

/*
 * setup 包读取：RM 56.4.6.4.2.1 的 SUTW 六步法。
 * 为什么需要 tripwire：setup 缓冲在 dQH 里，读它的同时新 setup 可能到达
 * 造成撕裂。SLOM=1（锁存关闭）+ SUTW（信号量）组合：抄完检查信号量仍
 * 置位 => 拷贝期间没被打断 => 数据可信。
 */
static int ep0_read_setup(u8 raw[8])
{
	struct usb_dqh *rx = &qh[QH_IDX(0, 0)];
	int retry;

	usbd_soc_wr32(R_ENDPTSETUPSTAT, 1);		/* a. W1C 清 setup 标志 */
	for (retry = 0; retry < 3; retry++) {		/* 有界：防主机 hammer 造成无声死锁 */
		usbd_soc_wr32(R_USBCMD, USBCMD_SUTW |	/* b. 信号量立起 */
				 (usbd_soc_rd32(R_USBCMD) & ~USBCMD_SUTW));
		xcopy(raw, (const void *)rx->setup, 8);	/* c. 抄 8 字节 */
		if (usbd_soc_rd32(R_USBCMD) & USBCMD_SUTW) {	/* d. 仍立起 = 撕裂未发生 */
			usbd_soc_wr32(R_USBCMD, usbd_soc_rd32(R_USBCMD) & ~USBCMD_SUTW);	/* e. 放倒 */
			return 0;
		}
	}
	printf("usbd: setup read unstable after 3 tries, using last copy\n");
	return 0;	/* 用最后一次拷贝继续（保守：宁可错应答也不挂死） */
}

/* 一整个 setup 事务：解码 -> 数据/状态阶段（RM 56.4.6.4.2 全流程） */
static void ep0_handle_setup(void)
{
	u8 raw[8];
	struct usb_setup s;
	struct usb_ch9_reply r;

	ep0_read_setup(raw);
	s.bmRequestType = raw[0];
	s.bRequest = raw[1];
	s.wValue = raw[2] | ((u16)raw[3] << 8);
	s.wIndex = raw[4] | ((u16)raw[5] << 8);
	s.wLength = raw[6] | ((u16)raw[7] << 8);

	/* GET_STATUS(endpoint)：应答数据是 ENDPTCTRL 里的 halt 位，协议层
	 * 不知道硬件——引擎自己组 2 字节。USB 2.0 9.4.5：wIndex = 端点地址
	 * （bit7 方向），被寻址端点的 Halt 回 byte0.bit0，byte1 保留 0
	 * （评审 M1：修前方向位被无视，0x81 被 STALL） */
	if ((s.bmRequestType & 0x1F) == 2 &&
	    s.bRequest == USB_REQ_GET_STATUS) {
		static u8 ep_st[2];
		u8 epnum = s.wIndex & 0x0F;
		u8 dir_in = (s.wIndex >> 7) & 1;
		int fail;

		if (s.wLength < 2 || (epnum != 0 && epnum != USB_BULK_EP)) {
			ep0_stall();
			return;
		}
		if (epnum == USB_BULK_EP) {
			u32 h = eng_ep1_halt_bits();

			ep_st[0] = (dir_in ? (h & 2) : (h & 1)) ? 1 : 0;
		} else {
			ep_st[0] = 0;			/* ep0 恒不 halt */
		}
		ep_st[1] = 0;
		fail = eng_ep_prime(0, 1, ep_st, 2) ||
		       eng_ep_wait_done(0, 1) ||
		       eng_ep_prime(0, 0, 0, 0);
		if (!fail)
			eng_ep_wait_done(0, 0);
		return;
	}

	usb_ch9_handle_setup(&s, &r);

	switch (r.action) {
	case USB_CH9_DATA_IN:
		/* 数据阶段：IN 方向发 reply 数据；随后状态阶段 OUT 方向收 ZLP。
		 * 任一步失败/被打断即整体作废——主机会重发 setup */
		if (eng_ep_prime(0, 1, r.data, r.len))
			break;
		if (eng_ep_wait_done(0, 1))
			break;
		if (eng_ep_prime(0, 0, 0, 0))	/* 状态 ZLP 从主机视角是 OUT */
			break;
		eng_ep_wait_done(0, 0);
		break;

	case USB_CH9_STATUS_IN:
		/* SET_ADDRESS 特例：借助 USBADRA 暂存，状态 ZLP 发完硬件才生效
		 * （规范要求新地址在状态阶段完成后才使用） */
		if (s.bRequest == USB_REQ_SET_ADDRESS) {
			u8 a = s.wValue & 0x7F;

			usbd_soc_wr32(R_DEVICEADDR, DEVICEADDR_USBADRA | ((u32)a << 25));
			dev_addr = a;
		}
		ep0_status_in();
		break;

	case USB_CH9_EP0_CLEAR_HALT:
		ep0_clear_stall();
		ep0_status_in();
		break;

	case USB_CH9_EP1_CLEAR_HALT:
		eng_ep1_halt_clear();
		ep0_status_in();
		break;

	case USB_CH9_EP1_SET_HALT:
		eng_ep1_set_stall(r.ep_dir);
		ep0_status_in();
		break;

	case USB_CH9_STALL:
	default:
		ep0_stall();
		break;
	}
}

/* 总线复位处理（RM 56.4.6.2.1：复位后地址回 0、状态机回默认） */
static void bus_reset(void)
{
	u32 flush = EPBIT(0, 0) | EPBIT(0, 1) | EPBIT(1, 0) | EPBIT(1, 1);
	u32 speed;
	u32 to = WAIT_LOOPS;

	dev_addr = 0;
	usbd_soc_wr32(R_DEVICEADDR, 0);
	ep1_sw_halt = 0;		/* host 请求的 halt 随复位解除 */
	usb_ch9_reset();		/* 协议层状态归零（configured 等，评审 M3） */

	/* 冲掉所有挂起的传输缓冲（buffer ready 被 USB reset 自动清，这里兜底） */
	usbd_soc_wr32(R_ENDPTFLUSH, flush);
	while (usbd_soc_rd32(R_ENDPTFLUSH) & flush) {
		if (--to == 0)
			break;
	}

	qh_reinit();					/* dQH 归零：解除一切挂起 */
	primed_len[0] = primed_len[1] = primed_len[2] = primed_len[3] = 0;
	eng_rst_cnt++;					/* 协议层感知复位、自愈状态机 */
	usbd_soc_wr32(R_ENDPTSETUPSTAT, 1);		/* W1C 清残留标志 */
	usbd_soc_wr32(R_ENDPTCOMPLETE, 0x30003);

	speed = (usbd_soc_rd32(R_PORTSC1) >> 26) & 3;
	printf("usbd: bus reset, port speed %s\n",
	       speed == 2 ? "high(480M)" : speed == 0 ? "full(12M)" : "?");
}

/* ---- 初始化（RM 56.4.6.1 五步） ---- */

static int usbd_hw_init(void)
{
	/* 步骤 0（时钟/PHY 是 RM 未展开的芯片级前置，SoC 层负责） */
	if (usbd_soc_clock_init()) {
		printf("usbd: clock init FAILED\n");
		return -1;
	}
	usbd_soc_phy_init();

	/* 步骤 1 前置：控制器软复位（RS 必须为 0，RST 硬件自清） */
	usbd_soc_wr32(R_USBCMD, 0);
	usbd_soc_wr32(R_USBCMD, USBCMD_RST);
	while (usbd_soc_rd32(R_USBCMD) & USBCMD_RST)
		;

	/* 步骤 1：USBMODE = 设备模式 + SLOM（=0xA）。RM：复位后只能写一次 */
	usbd_soc_wr32(R_USBMODE, USBMODE_CM_DEV | USBMODE_SLOM);

	/* 步骤 2/3：dQH 数组 + 列表地址（64 字节对齐，RM 56.6.25） */
	qh_reinit();
	usbd_soc_wr32(R_ENDPTLISTADDR, (u32)qh);

	/* EP0：RM 56.4.6.1 明言免配 ENDPTCTRL0（复位值 0080_0080 已使能收发、类型固定） */
	/* EP1 bulk：RM Table 56-65——type=10 且上下半一致，RXE/TXE 使能 */
	usbd_soc_wr32(R_ENDPTCTRL0 + 4 * USB_BULK_EP,
		      EPCTRL_RXT_BULK | EPCTRL_RXE | EPCTRL_TXT_BULK | EPCTRL_TXE);

	/* 轮询模式：中断全屏蔽（RM 步骤 4 的替代实现） */
	usbd_soc_wr32(R_USBINTR, 0);

	/* 清残留状态 */
	usbd_soc_wr32(R_ENDPTSETUPSTAT, 1);
	usbd_soc_wr32(R_ENDPTCOMPLETE, 0x30003);

	/* 步骤 5：RS=1 —— D+ 上拉出现，主机（PC）此刻才看到设备插入 */
#if USB_FORCE_FULL_SPEED
	/* PORTSC.PFSC(24)：强制全速连接（诊断期开关，见 usb_config.h） */
	usbd_soc_wr32(R_PORTSC1, usbd_soc_rd32(R_PORTSC1) | BIT(24));
#endif
	usbd_soc_wr32(R_USBCMD, USBCMD_RS);
	return 0;
}

/* ---- 对外入口 ---- */

void usbd_download_mode(void)
{
	printf("usbd: download mode, VID:PID=%04x:%04x product=\"%s\"\n",
	       USB_VENDOR_ID, USB_PRODUCT_ID, USB_PRODUCT);

	usb_ch9_init();		/* ascii 名字 -> UTF-16 描述符（漏调则字符串全为 0 长） */

	if (usbd_hw_init()) {
		printf("usbd: init failed, halt\n");
		for (;;)
			;
	}
	printf("usbd: waiting for host (plug OTG port)...\n");

	usb_dl_start();			/* 识别并初始化烧写目标介质 */

	/* 轮询主循环：永不返回。EP0=USB 枚举，EP1=JBDL 烧录协议 */
	for (;;) {
		u32 sts = usbd_soc_rd32(R_USBSTS);

		if (sts & USBSTS_URI) {
			bus_reset();
		}
		if (usbd_soc_rd32(R_ENDPTSETUPSTAT) & 1) {
			ep0_handle_setup();
		}
		usb_dl_poll();		/* EP1 命令/数据状态机 */

		/* W1C 清已见过的状态位（错误位也清掉，防重复进错误路径） */
		usbd_soc_wr32(R_USBSTS, sts & USBSTS_W1C_MASK);
	}
}
