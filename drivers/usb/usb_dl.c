// SPDX-License-Identifier: MIT
/*
 * USB 下载协议 JBDL v1 —— bulk EP1 上的命令/应答状态机（轮询驱动）。
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
#include <blocksrc.h>
#include <bootdev.h>
#include "usb_eng.h"
#include "usb_dl.h"

#define JBDL_MAGIC	0x4C44424AU	/* "JBDL" 小端 */
#define JBDL_VER	1
enum { JBDL_PING = 1, JBDL_INFO = 2, JBDL_READ = 3, JBDL_WRITE = 4,
       JBDL_WRITE_DONE = 5 };
#define JBDL_HDR_LEN	32
#define JBDL_CHUNK	(16 * 1024)
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
static u8 data_buf[JBDL_CHUNK] __attribute__((aligned(32)));
static int rx_armed;				/* EP1 OUT 已 prime 未完成 */
static u32 last_reset;				/* 引擎复位计数镜像 */
enum dl_state { ST_HDR, ST_WDATA };
static enum dl_state state;
static u32 cur_lba, blocks_left;		/* WRITE 进行中的游标 */
static u32 werr;				/* WRITE 过程中的错误码 */

static void hdr_to_buf(u8 *p, u8 cmd, u8 status, u32 lba, u32 count, u32 seq)
{
	struct jbdl_hdr h = {
		.magic = JBDL_MAGIC, .ver = JBDL_VER,
		.cmd = cmd, .status = status,
		.lba = lba, .count = count, .seq = seq,
	};

	__builtin_memcpy(p, &h, sizeof(h));
}

/* 应答发送（阻塞；主机不收的可能性由复位计数兜底退出） */
/* 发送超时（忙等圈数 ~15s 量级）：主机读走或总线复位二选一，
 * 都没有（主机进程被杀且未触发 reset）就放弃，防永久空转 */
#define DL_TX_TIMEOUT_LOOPS	120000000u

static int dl_tx(const void *p, u32 len)
{
	u32 to = DL_TX_TIMEOUT_LOOPS;

	if (eng_ep_prime(1, 1, p, len))
		return -1;
	while (to--) {
		if (eng_reset_count() != last_reset)
			return -1;		/* 总线复位：放弃本应答 */
		if (eng_ep_check_done(1, 1))
			return 0;
	}
	printf("usbdl: tx timeout, host stalled\n");
	return -1;				/* 主机停读：放弃本次传输 */
}

static void dl_rx_arm(u32 len)
{
	eng_ep_prime(1, 0, (state == ST_HDR) ? hdr_buf : data_buf, len);
	rx_armed = 1;
}

static u32 dl_rx_len(void)
{
	return eng_ep_rx_len(1, 0);
}

/* 复位/状态自愈检查：计数变了就丢弃全部进行中状态 */
static int dl_reset_seen(void)
{
	if (eng_reset_count() != last_reset) {
		last_reset = eng_reset_count();
		rx_armed = 0;
		state = ST_HDR;
		return 1;
	}
	return 0;
}

static void handle_hdr(u32 len)
{
	struct jbdl_hdr h;

	if (len != JBDL_HDR_LEN) {
		state = ST_HDR;			/* 长度不对：丢弃重收 */
		return;
	}
	__builtin_memcpy(&h, hdr_buf, sizeof(h));
	if (h.magic != JBDL_MAGIC || h.ver != JBDL_VER) {
		state = ST_HDR;
		return;
	}

	switch (h.cmd) {
	case JBDL_PING:
		hdr_to_buf(hdr_buf, JBDL_PING, 0x4F, 0, 0, 0);
		if (dl_tx(hdr_buf, JBDL_HDR_LEN))
			return;
		break;

	case JBDL_INFO: {
		static char info[96];
		u32 ilen;

		/* 容量解析需 CSD，v1 先给介质名+协议版本（够 flashtool 展示身份） */
		ilen = (u32)snprintf(info, sizeof(info),
				     "justiceBL dl v%d media=%s", JBDL_VER,
				     src ? src->name : "none");
		hdr_to_buf(hdr_buf, JBDL_INFO, JBDL_ST_OK, 0, ilen, 0);
		if (dl_tx(hdr_buf, JBDL_HDR_LEN))
			return;
		if (dl_tx(info, ilen))
			return;
		break;
	}

	case JBDL_READ: {
		u32 done = 0;

		if (!src) {
			hdr_to_buf(hdr_buf, JBDL_READ, JBDL_ST_ERR, 0, 0, 0);
			if (dl_tx(hdr_buf, JBDL_HDR_LEN))
				return;
			break;
		}
		hdr_to_buf(hdr_buf, JBDL_READ, JBDL_ST_OK, h.lba, h.count, 0);
		if (dl_tx(hdr_buf, JBDL_HDR_LEN))
			return;
		while (done < h.count) {
			u32 n = h.count - done;
			u32 bytes;

			if (dl_reset_seen())
				return;
			if (n > JBDL_CHUNK / 512)
				n = JBDL_CHUNK / 512;
			if (src->read(h.lba + done, n, data_buf, 0)) {
				/* 读错：中断传输（host 超时收场）。v1 协议
				 * 头已回 OK 无法中途改报错，只能弃流 */
				printf("usbdl: media read fail @lba%u, abort stream\n",
				       h.lba + done);
				break;
			}
			bytes = n * 512;
			if (dl_tx(data_buf, bytes))
				return;
			done += n;
		}
		break;
	}

	case JBDL_WRITE:
		if (!src || !src->write) {
			hdr_to_buf(hdr_buf, JBDL_WRITE, JBDL_ST_ERR, 0, 0, 0);
			if (dl_tx(hdr_buf, JBDL_HDR_LEN))
				return;
			break;
		}
		hdr_to_buf(hdr_buf, JBDL_WRITE, JBDL_ST_OK, h.lba, h.count, 0);
		if (dl_tx(hdr_buf, JBDL_HDR_LEN))
			return;
		cur_lba = h.lba;
		blocks_left = h.count;
		werr = 0;
		if (!blocks_left) {		/* 空写：ack 后立即 DONE，不留等待态 */
			hdr_to_buf(hdr_buf, JBDL_WRITE_DONE, JBDL_ST_OK, h.lba, 0, 0);
			if (dl_tx(hdr_buf, JBDL_HDR_LEN))
				return;
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

static void handle_wdata(u32 len)
{
	u32 blocks = len / 512;

	if (len != blocks * 512) {
		werr = 1;			/* 短包：按错收处理，等 host 收 DONE */
		blocks_left = 0;
	} else if (src->write(cur_lba, blocks, data_buf, 0)) {
		werr = 2;
		blocks_left = 0;
	} else {
		cur_lba += blocks;
		blocks_left -= blocks;
	}
	if (!blocks_left) {
		hdr_to_buf(hdr_buf, JBDL_WRITE_DONE, werr ? JBDL_ST_ERR : JBDL_ST_OK,
			   cur_lba, 0, 0);
		if (dl_tx(hdr_buf, JBDL_HDR_LEN))
			return;
		state = ST_HDR;
	}
}

void usb_dl_start(void)
{
	src = boot_source_get();
	if (src && src->init()) {
		printf("usbdl: media init failed (%s)\n", src->name);
		src = 0;
	}
	if (src && !src->write)
		printf("usbdl: media is read-only\n");
	if (src)
		printf("usbdl: target media %s\n", src->name);
}

void usb_dl_poll(void)
{
	if (!src)
		return;
	if (dl_reset_seen())
		return;

	switch (state) {
	case ST_HDR:
		if (!rx_armed) {
			dl_rx_arm(JBDL_HDR_LEN);
			break;
		}
		if (eng_ep_check_done(1, 0)) {
			u32 len = dl_rx_len();

			rx_armed = 0;
			handle_hdr(len);
		}
		break;

	case ST_WDATA:
		if (!rx_armed) {
			u32 bytes = blocks_left * 512;

			if (bytes > JBDL_CHUNK)
				bytes = JBDL_CHUNK;
			dl_rx_arm(bytes);
			break;
		}
		if (eng_ep_check_done(1, 0)) {
			u32 len = dl_rx_len();

			rx_armed = 0;
			handle_wdata(len);
		}
		break;
	}
}
