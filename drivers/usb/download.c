// SPDX-License-Identifier: MIT
/*
 * USB 下载协议 JBDL v1 —— bulk EP1 上的命令/应答状态机（轮询驱动）。
 *
 * 数据通路（★乒乓流水线，USB 收与 SD 写/读并行）：
 *
 *   写方向：主机 16K ─USB DMA─▶ chunk[A] ─CPU PIO─▶ SD 卡
 *           主机下个 16K ─DMA─▶ chunk[B]（B 的接收在写 A 前已提交）
 *   读方向：CPU 读 chunk[B] ←─SD PIO     同时 USB DMA 发 chunk[A] ─▶ 主机
 *
 * 请求模型：每次收/发是一个 usb_xfer 对象（见 usb_eng.h）；提交后在途
 * 状态由引擎跟踪，本层不持影子标志。usb_ep_poll 三态：-1=未提交（该
 * arm 了）/0=在途/1=完成。
 *
 * 耦合规则：只经 usb_eng.h（引擎）与 blocksrc.h（介质）两个接口，不含
 * 任何寄存器地址；介质 = boot_source_get()（烧写目标 = 启动介质本身）。
 *
 * 线格式（32 字节定长头，可带数据阶段）：
 *   struct jbdl_hdr { magic "JBDL" | ver | cmd | status | lba | count |
 *                     crc(预留) | seq | rsv[2] }
 * 命令（host -> device）：
 *   PING(1)   心跳；应答头 status=0x4F
 *   INFO(2)   应答头(count=载荷字节数) + 信息字符串（介质/容量/协议版本）
 *   READ(3)   lba/count -> 应答头(status=0) + count*512B 分块 IN
 *   WRITE(4)  lba/count -> 应答头(status=0 收数准备) + host 分块 OUT，
 *             全部收完 -> WRITE_DONE 应答头（status 非 0 = 写错）
 * 负载分块 16KB/次（dTD 上限 16K）；头里 crc 字段 v1 预留（USB 包级
 * CRC16 已保真，端到端校验由 host 烧后 READ 回读比对）。
 */

#include <types.h>
#include <printf.h>
#include <usb_config.h>
#include <blocksrc.h>
#include <bootdev.h>
#include "usb_eng.h"
#include "download.h"

#define JBDL_MAGIC	0x4C44424AU	/* "JBDL" 小端 */
#define JBDL_VER	1
enum { JBDL_PING = 1, JBDL_INFO = 2, JBDL_READ = 3, JBDL_WRITE = 4,
       JBDL_WRITE_DONE = 5 };
#define JBDL_HDR_LEN	32
#define JBDL_CHUNK	(16 * 1024)
#define JBDL_CHUNK_BLOCKS	(JBDL_CHUNK / 512)
#define JBDL_ST_OK	0x00
#define JBDL_ST_ERR	0x01

struct jbdl_hdr {
	u32	magic;
	u16	ver;
	u8	cmd;
	u8	status;
	u32	lba;
	u32	count;		/* READ/WRITE: 块数；INFO 应答: 载荷字节数 */
	u32	crc;
	u32	seq;
	u32	rsv[2];
} __attribute__((packed));

static const struct block_source *src;		/* 烧写目标 = 启动介质 */
static u8 hdr_buf[JBDL_HDR_LEN] __attribute__((aligned(32)));
static u8 chunk[2][JBDL_CHUNK] __attribute__((aligned(32)));	/* ★乒乓双桶 */
static struct usb_xfer hdr_xfer;		/* ST_HDR 在途接收 */
static struct usb_xfer rx_xfer;			/* ST_WDATA 在途接收 */
static u8 rx_slot;				/* 在途接收落在哪个桶 */
static u32 last_link_generation;		/* 链路代际计数镜像 */
enum download_state { ST_HDR, ST_WDATA };
static enum download_state state;
static u32 cur_lba, blocks_left;		/* WRITE 进行中的游标 */
static u32 werr;				/* WRITE 过程中的错误码 */

/* 发送超时（忙等圈数 ~15s 量级）：主机读走或总线复位二选一，
 * 都没有（主机进程被杀且未触发 reset）就放弃，防永久空转 */
#define DL_TX_TIMEOUT_LOOPS	120000000u

static void download_build_header(u8 *p, u8 cmd, u8 status, u32 lba, u32 count)
{
	struct jbdl_hdr h = {
		.magic = JBDL_MAGIC, .ver = JBDL_VER,
		.cmd = cmd, .status = status,
		.lba = lba, .count = count,
	};

	__builtin_memcpy(p, &h, sizeof(h));
}

/* 链路代际变了就丢弃全部进行中状态（总线复位/flush 后的自愈入口） */
static int download_link_changed(void)
{
	if (usb_link_generation() != last_link_generation) {
		last_link_generation = usb_link_generation();
		state = ST_HDR;
		return 1;
	}
	return 0;
}

/* 阻塞发送（应答头等短载荷）：提交 -> 轮询完成；主机停读或复位则放弃 */
static int download_send(const void *p, u32 len)
{
	struct usb_xfer tx = { .buf = (void *)p, .len = len };
	u32 to = DL_TX_TIMEOUT_LOOPS;

	if (usb_ep_queue(USB_BULK_EP, 1, &tx))
		return -1;
	while (usb_ep_poll(USB_BULK_EP, 1, &tx) == 0) {
		if (download_link_changed())
			return -1;		/* 总线复位：放弃本应答 */
		if (--to == 0) {
			printf("download: tx timeout, host stalled\n");
			return -1;		/* 主机停读：放弃本次传输 */
		}
	}
	return tx.err ? -1 : 0;
}

/* 等在途发送完成（READ 流水线用）：1=完成，0=复位/超时/错误弃流 */
static int download_tx_drain(struct usb_xfer *tx)
{
	u32 to = DL_TX_TIMEOUT_LOOPS;
	int ret;

	while ((ret = usb_ep_poll(USB_BULK_EP, 1, tx)) == 0) {
		if (download_link_changed())
			return 0;
		if (--to == 0) {
			printf("download: tx timeout, host stalled\n");
			return 0;
		}
	}
	return (ret == 1 && tx->err == 0) ? 1 : 0;
}

static void download_handle_command(const u8 *raw)
{
	struct jbdl_hdr h;

	__builtin_memcpy(&h, raw, sizeof(h));
	if (h.magic != JBDL_MAGIC || h.ver != JBDL_VER)
		return;				/* 非法头：丢弃重收 */

	/* H1 边界校验（64 位防 u32 回绕）：越界/回绕一律 ST_ERR，
	 * 让 host 端拿到确定性错误而非 30s 超时+弃流 */
	if ((h.cmd == JBDL_READ || h.cmd == JBDL_WRITE) && src) {
		u32 cap = src->blocks ? src->blocks() : 0;

		if (cap && (u64)h.lba + (u64)h.count > cap) {
			download_build_header(hdr_buf, h.cmd, JBDL_ST_ERR,
					      h.lba, h.count);
			if (download_send(hdr_buf, JBDL_HDR_LEN))
				return;
			printf("download: %s out-of-range lba=%u count=%u cap=%u\n",
			       h.cmd == JBDL_READ ? "READ" : "WRITE",
			       h.lba, h.count, cap);
			return;
		}
	}

	switch (h.cmd) {
	case JBDL_PING:
		download_build_header(hdr_buf, JBDL_PING, 0x4F, 0, 0);
		download_send(hdr_buf, JBDL_HDR_LEN);
		break;

	case JBDL_INFO: {
		static char info[112];
		u32 ilen;
		u32 cap = (src && src->blocks) ? src->blocks() : 0;

		ilen = (u32)snprintf(info, sizeof(info),
				     "justiceBL dl v%d media=%s cap=%u blocks",
				     JBDL_VER, src ? src->name : "none", cap);
		if (ilen >= sizeof(info))
			ilen = sizeof(info) - 1;	/* 截断钳位：防越界读 */
		download_build_header(hdr_buf, JBDL_INFO, JBDL_ST_OK, 0, ilen);
		if (download_send(hdr_buf, JBDL_HDR_LEN))
			return;
		download_send(info, ilen);
		break;
	}

	case JBDL_READ: {
		/* 流水线：CPU 读 chunk[i^1]（SD 慢）与 USB 发 chunk[i] 重叠。
		 * 主机持续在收（IN 流不缺接收请求），重叠纯为提速 */
		struct usb_xfer tx;
		u32 done = 0, n;
		u8 slot = 0;

		if (!src) {
			download_build_header(hdr_buf, JBDL_READ, JBDL_ST_ERR,
					      0, 0);
			download_send(hdr_buf, JBDL_HDR_LEN);
			break;
		}
		download_build_header(hdr_buf, JBDL_READ, JBDL_ST_OK,
				      h.lba, h.count);
		if (download_send(hdr_buf, JBDL_HDR_LEN))
			return;

		/* 首块：读 + 提交 */
		n = h.count;
		if (n > JBDL_CHUNK_BLOCKS)
			n = JBDL_CHUNK_BLOCKS;
		if (src->read(h.lba, n, chunk[0], 0)) {
			printf("download: media read fail @lba%u, abort stream\n",
			       h.lba);
			break;
		}
		tx.buf = chunk[0];
		tx.len = n * 512;
		if (usb_ep_queue(USB_BULK_EP, 1, &tx))
			return;
		done = n;
		slot = 1;

		while (done < h.count) {
			if (download_link_changed())
				return;
			n = h.count - done;
			if (n > JBDL_CHUNK_BLOCKS)
				n = JBDL_CHUNK_BLOCKS;
			/* ① CPU 读下一块（USB 正在发上一块——重叠） */
			if (src->read(h.lba + done, n, chunk[slot], 0)) {
				printf("download: media read fail @lba%u, abort stream\n",
				       h.lba + done);
				download_tx_drain(&tx);	/* 等在途发完再断流 */
				return;
			}
			/* ② 等上一块发完 */
			if (!download_tx_drain(&tx))
				return;
			/* ③ 提交本块 */
			tx.buf = chunk[slot];
			tx.len = n * 512;
			if (usb_ep_queue(USB_BULK_EP, 1, &tx))
				return;
			done += n;
			slot ^= 1;
		}
		download_tx_drain(&tx);		/* 尾：等最后一块发完 */
		break;
	}

	case JBDL_WRITE:
		if (!src || !src->write) {
			download_build_header(hdr_buf, JBDL_WRITE, JBDL_ST_ERR,
					      0, 0);
			download_send(hdr_buf, JBDL_HDR_LEN);
			break;
		}
		download_build_header(hdr_buf, JBDL_WRITE, JBDL_ST_OK,
				      h.lba, h.count);
		if (download_send(hdr_buf, JBDL_HDR_LEN))
			return;
		cur_lba = h.lba;
		blocks_left = h.count;
		werr = 0;
		rx_slot = 0;
		if (!blocks_left) {		/* 空写：ack 后立即 DONE */
			download_build_header(hdr_buf, JBDL_WRITE_DONE,
					      JBDL_ST_OK, h.lba, 0);
			download_send(hdr_buf, JBDL_HDR_LEN);
			state = ST_HDR;
			return;
		}
		state = ST_WDATA;
		return;				/* 状态已切换，不落 ST_HDR */

	default:
		break;
	}
	state = ST_HDR;
}

void download_media_init(void)
{
	src = boot_source_get();
	if (src && src->init()) {
		printf("download: media init failed (%s)\n", src->name);
		src = 0;
	}
	if (src && !src->write)
		printf("download: media is read-only\n");
	if (src)
		printf("download: target media %s\n", src->name);
}

void download_poll(void)
{
	/* 介质不可用（无卡/init 失败）时协议层不全哑——PING 照常 ack，
	 * READ/WRITE/INFO 走 handle_command 的 !src 错误分支 */

	/* EP1 意外 stall 自愈（谁置的一打便知：打印一次并清除）。
	 * 主机侧表现为 LIBUSB_ERROR_PIPE——不清则 WinUSB 永久 halt 管道 */
	{
		u32 st = usb_ep1_clear_unexpected_stall();

		if (st) {
			printf("download: ep1 stall detected (%s%s) cleared\n",
			       (st & 0xFFFF) ? "rx" : "",
			       (st >> 16) ? "+tx" : "");
		}
	}

	if (download_link_changed())
		return;

	switch (state) {
	case ST_HDR:
		switch (usb_ep_poll(USB_BULK_EP, 0, &hdr_xfer)) {
		case -1:			/* 未提交：arm 32 字节头接收 */
			hdr_xfer.buf = hdr_buf;
			hdr_xfer.len = JBDL_HDR_LEN;
			usb_ep_queue(USB_BULK_EP, 0, &hdr_xfer);
			break;
		case 0:
			break;			/* 在途 */
		case 1:
			/* 完成一个头：长度不对/dTD 报错按丢处理——poll 下一
			 * 轮返回 -1 自动重 arm（报错头内容不可信，解析可能
			 * 撞出合法命令） */
			if (hdr_xfer.actual == JBDL_HDR_LEN && !hdr_xfer.err)
				download_handle_command(hdr_buf);
			break;
		}
		break;

	case ST_WDATA:
		switch (usb_ep_poll(USB_BULK_EP, 0, &rx_xfer)) {
		case -1: {			/* 未提交：按剩余量 arm 乒乓槽 */
			u32 want = blocks_left;

			if (want > JBDL_CHUNK_BLOCKS)
				want = JBDL_CHUNK_BLOCKS;
			rx_xfer.buf = chunk[rx_slot];
			rx_xfer.len = want * 512;
			usb_ep_queue(USB_BULK_EP, 0, &rx_xfer);
			break;
		}
		case 0:
			break;			/* 在途 */

		case 1: {
			/* 完成一个 chunk。三步：
			 * ① 立即提交下一块接收（乒乓翻转）——USB 收与下面
			 *    SD 写并行，主机不再被 NAK 卡住；
			 * ② 同步写刚收满的桶；
			 * ③ 计数推进，收尾清场发 DONE */
			u8 filled = rx_slot;
			u32 n = rx_xfer.actual;
			u32 left;

			if (rx_xfer.err) {
				printf("download: OUT dTD err=0x%x\n",
				       rx_xfer.err);
				werr = 3;
			} else if (n == 0 || n % 512) {
				werr = 1;	/* 短包：按错收处理 */
			}

			left = blocks_left - (werr ? 0 : n / 512);
			if (!werr && left > 0) {
				u32 want = left;

				if (want > JBDL_CHUNK_BLOCKS)
					want = JBDL_CHUNK_BLOCKS;
				rx_slot ^= 1;
				rx_xfer.buf = chunk[rx_slot];
				rx_xfer.len = want * 512;
				usb_ep_queue(USB_BULK_EP, 0, &rx_xfer);
			}

			if (!werr && src->write(cur_lba, n / 512,
						chunk[filled], 0)) {
				printf("download: media write err lba=%u blocks=%u\n",
				       cur_lba, n / 512);
				werr = 2;
			}
			if (!werr) {
				cur_lba += n / 512;
				blocks_left = left;
			} else {
				blocks_left = 0;
			}

			if (!blocks_left) {
				/* 清场：作废可能在途的接收（正常收尾无在途，
				 * flush 无害；写失败路径必须作废，否则残留
				 * 完成位会被 ST_HDR 的头接收误读） */
				usb_ep1_rx_abort();
				download_build_header(hdr_buf, JBDL_WRITE_DONE,
						      werr ? JBDL_ST_ERR :
							     JBDL_ST_OK,
						      cur_lba, 0);
				download_send(hdr_buf, JBDL_HDR_LEN);
				state = ST_HDR;
			}
			break;
		}
		}
		break;
	}
}
