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
 *   INFO(2)   应答头(count=载荷字节数) + 多行文本：
 *             boot=<启动介质名> 与 media<N>=<名> present=<0/1> cap=<块数>
 *             （两介质在线探测一次拿全，容量单位 512B 块）
 *   READ(3)   lba/count -> 应答头(status=0) + count*512B 分块 IN
 *   WRITE(4)  lba/count -> 应答头(status=0 收数准备) + host 分块 OUT，
 *             全部收完 -> WRITE_DONE 应答头（status 非 0 = 写错）
 *   SELECT(6) count=0/1 切烧写目标介质 -> 应答头(status=0)
 *             默认 0=启动介质；切到不在位的介质回 status=ERR。
 *             整卡烧录（flashtool 恢复页）用它指 SD 或 eMMC；
 *             老工具不发此命令，行为不变（向后兼容，版本号不动）
 * 负载分块 16KB/次（dTD 上限 16K）；头里 crc 字段 v1 预留（USB 包级
 * CRC16 已保真，端到端校验由 host 烧后 READ 回读比对）。
 */

#include <types.h>
#include <stddef.h>
#include <printf.h>
#include <timer.h>
#include <math.h>
#include <usb_config.h>
#include <blocksrc.h>
#include <bootdev.h>
#include "usb_eng.h"
#include "download.h"

#define JBDL_MAGIC	0x4C44424AU	/* "JBDL" 小端 */
#define JBDL_VER	1
enum { JBDL_PING = 1, JBDL_INFO = 2, JBDL_READ = 3, JBDL_WRITE = 4,
       JBDL_WRITE_DONE = 5, JBDL_SELECT = 6, JBDL_FLUSH = 7 };
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

static const struct block_source *src;		/* 当前烧写目标（SELECT 可切） */
static int media_ready;			/* 0=待探测（lazy），1=已探测 */

/* 双介质槽：0=启动介质（拨码决定 SD/eMMC），1=另一张。INFO 报全表，
 * SELECT 切换；块源实例来自 blocksrc.h（sd_source/emmc_source） */
struct media_slot {
	const struct block_source *source;
	int present;				/* init 成功 = 在位 */
	u32 blocks;				/* 在位时的容量（512B 块） */
	char detail[160];			/* 身份/健康单行（block_source
						 * .detail；不在位时留空） */
};
static struct media_slot media[2];
static const char *boot_media_name;		/* 启动介质名（INFO 首行） */
static u8 hdr_buf[JBDL_HDR_LEN] __attribute__((aligned(32)));
/* ★乒乓双桶——DDR 大数据区（32K 挤爆 OCRAM 64K 的元凶，2026-11-19 下沉；
 * DDR 由 DCD 在 ROM 阶段初始化，与 U-Boot 落点同块活内存） */
static u8 chunk[2][JBDL_CHUNK] __attribute__((aligned(32), section(".ddr_bss")));
static struct usb_xfer hdr_xfer;		/* ST_HDR 在途接收 */
static struct usb_xfer rx_xfer;			/* ST_WDATA 在途接收 */
static u8 rx_slot;				/* 在途接收落在哪个桶 */
static u32 last_link_generation;		/* 链路代际计数镜像 */
enum download_state { ST_HDR, ST_WDATA };
static enum download_state state;
static u32 cur_lba, blocks_left;		/* WRITE 进行中的游标 */
static u32 werr;				/* WRITE 过程中的错误码 */

/* ---- 分段耗时统计（无干扰版）：timer_counter 计时存内存，
 * 事务收尾一次性输出（传输期间零 UART 占用）。每事务采样前 16 chunk ---- */
#define DIAG_N			16
static u32 diag_rx_us[DIAG_N];		/* WRITE：USB 接收耗时 */
static u32 diag_mmc_us[DIAG_N];		/* WRITE：eMMC 写（PIO+命令）耗时 */
static u32 diag_n;			/* 已采样数 */
static u64 diag_t_arm;			/* chunk 武装时刻 */
static u32 diag_rm_us[DIAG_N];		/* READ：eMMC 读耗时 */
static u32 diag_ru_us[DIAG_N];		/* READ：USB 发送等待耗时 */
static u32 diag_rn;			/* READ 已采样数 */

/* ticks → µs。诊断窗口短（<1s），差值截 u32 后走 udiv32
 * （裸机无 libgcc，u64/u32 除法会拉 __aeabi_uidiv） */
static u32 diag_us(u64 from, u64 to)
{
	return udiv32((u32)(to - from), timer_freq_hz() / 1000000U);
}

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
		static char info[512];
		u32 ilen;

		ilen = (u32)snprintf(info, sizeof(info),
				     "justiceBL dl v%d boot=%s\n"
				     "media0=%s present=%d cap=%u\n"
				     "media0.detail=%s\n"
				     "media1=%s present=%d cap=%u\n"
				     "media1.detail=%s",
				     JBDL_VER,
				     boot_media_name ? boot_media_name : "none",
				     media[0].source ? media[0].source->name : "none",
				     media[0].present, media[0].blocks,
				     media[0].present ? media[0].detail : "-",
				     media[1].source ? media[1].source->name : "none",
				     media[1].present, media[1].blocks,
				     media[1].present ? media[1].detail : "-");
		if (ilen >= sizeof(info))
			ilen = sizeof(info) - 1;	/* 截断钳位：防越界读 */
		download_build_header(hdr_buf, JBDL_INFO, JBDL_ST_OK, 0, ilen);
		if (download_send(hdr_buf, JBDL_HDR_LEN))
			return;
		download_send(info, ilen);
		break;
	}

	case JBDL_SELECT:
		/* 整卡烧录选目标：count=0/1。只允许切到在位介质——
		 * 切了空卡槽，后续 WRITE 全部越界拒烧，不如当场报错 */
		if (h.count < 2 && media[h.count].present) {
			src = media[h.count].source;
			download_build_header(hdr_buf, JBDL_SELECT,
					      JBDL_ST_OK, 0, h.count);
		} else {
			download_build_header(hdr_buf, JBDL_SELECT,
					      JBDL_ST_ERR, 0, h.count);
		}
		download_send(hdr_buf, JBDL_HDR_LEN);
		break;

	case JBDL_FLUSH:
		/* 任务级缓存落盘：烧录任务收尾时主机显式发一次。
		 * count=0：flush 当前所选介质 */
		if (src && src->flush) {
			if (src->flush())
				download_build_header(hdr_buf, JBDL_FLUSH,
						      JBDL_ST_ERR, 0, 0);
			else
				download_build_header(hdr_buf, JBDL_FLUSH,
						      JBDL_ST_OK, 0, 0);
		} else {
			download_build_header(hdr_buf, JBDL_FLUSH,
					      JBDL_ST_OK, 0, 0);
		}
		download_send(hdr_buf, JBDL_HDR_LEN);
		break;

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
		diag_rn = 0;			/* 新 READ 事务：采样窗口重开 */

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
			u64 t_mmc0, t_usb0;

			if (download_link_changed())
				return;
			n = h.count - done;
			if (n > JBDL_CHUNK_BLOCKS)
				n = JBDL_CHUNK_BLOCKS;
			/* ① CPU 读下一块（USB 正在发上一块——重叠） */
			t_mmc0 = timer_counter();
			if (src->read(h.lba + done, n, chunk[slot], 0)) {
				printf("download: media read fail @lba%u, abort stream\n",
				       h.lba + done);
				download_tx_drain(&tx);	/* 等在途发完再断流 */
				return;
			}
			/* ② 等上一块发完 */
			t_usb0 = timer_counter();
			if (!download_tx_drain(&tx))
				return;
			/* 采样（每 READ 事务前 16 chunk） */
			if (diag_rn < DIAG_N) {
				diag_rm_us[diag_rn] = diag_us(t_mmc0,
							      t_usb0);
				diag_ru_us[diag_rn] = diag_us(t_usb0,
							      timer_counter());
				diag_rn++;
			}
			/* ③ 提交本块 */
			tx.buf = chunk[slot];
			tx.len = n * 512;
			if (usb_ep_queue(USB_BULK_EP, 1, &tx))
				return;
			done += n;
			slot ^= 1;
		}
		download_tx_drain(&tx);		/* 尾：等最后一块发完 */
		/* 事务收尾：一次性输出采样 */

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
		diag_n = 0;			/* 新 WRITE 事务：采样窗口重开 */
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

/* 探测一个介质：init 成功 = 在位并记容量。失败打一行返回码（哪步死
 * 的看 emmc_init/sd_init 内部打印——静默吞错是无声失败剧本，eMMC 首板
 * 探测 2026-11-18 实证：只报 absent 无从下手） */
static void media_probe(struct media_slot *slot,
			const struct block_source *source, const char *tag)
{
	int ret;

	slot->source = source;
	slot->present = 0;
	slot->blocks = 0;
	slot->detail[0] = 0;
	if (!source || !source->init)
		return;
	ret = source->init();
	printf("download: %s init ret=%d\n", tag, ret);	/* ★诊断 */
	if (ret) {
		printf("download: %s init fail ret=%d\n", tag, ret);
		return;
	}
	slot->present = 1;
	slot->blocks = source->blocks ? source->blocks() : 0;
	printf("download: %s probe ok, detail...\n", tag);	/* ★诊断 */
	if (source->detail) {
		const char *d = source->detail();

		printf("download: %s detail len=%u\n", tag,	/* ★诊断 */
		       (u32)sizeof(slot->detail));
		for (u32 i = 0; i + 1 < sizeof(slot->detail) && d[i]; i++)
			slot->detail[i] = d[i];
		slot->detail[sizeof(slot->detail) - 1] = 0;
	}
	printf("download: %s probe done\n", tag);			/* ★诊断 */
}

void download_media_init(void)
{
	const struct block_source *boot;
	const struct block_source *other;

	if (media_ready)
		return;			/* 幂等：lazy 首次调用后不再重跑 */

	boot = boot_source_get();
	other = (boot == &sd_source) ? &emmc_source : &sd_source;

	media_probe(&media[0], boot, "media0");
	media_probe(&media[1], other, "media1");
	boot_media_name = boot ? boot->name : "none";

	/* 默认烧写目标 = 启动介质；不在位（异常）退而选在位的另一张 */
	src = media[0].present ? media[0].source :
	      (media[1].present ? media[1].source : NULL);
	if (src && !src->write)
		printf("download: media is read-only\n");
	printf("download: media0 %s%s\n", boot_media_name,
	       media[0].present ? "" : "(absent)");
	printf("download: media1 %s%s\n", other->name,
	       media[1].present ? "" : "(absent)");
	printf("download: target media %s\n", src ? src->name : "none");
	media_ready = 1;
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
			diag_t_arm = timer_counter();	/* USB 接收起点 */
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
			u64 t_done = timer_counter();	/* USB 接收完成时刻 */
			u64 t_mmc0;

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
				diag_t_arm = timer_counter();	/* 下一块接收起点 */
			}

			t_mmc0 = timer_counter();	/* eMMC 写起点 */
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

			/* 采样（每 WRITE 事务前 16 chunk） */
			if (diag_n < DIAG_N) {
				diag_rx_us[diag_n] = diag_us(diag_t_arm,
							     t_done);
				diag_mmc_us[diag_n] = diag_us(t_mmc0,
							      timer_counter());
				diag_n++;
			}

			if (!blocks_left) {
				/* 事务收尾：一次性输出采样（主机在等
				 * WRITE_DONE，此处 UART 阻塞无害） */
				for (u32 i = 0; i < diag_n; i++) {
					printf("download: chunk%u usb_rx=%uus "
					       "mmc_write=%uus\n",
					       i, diag_rx_us[i],
					       diag_mmc_us[i]);
				}
				/* 清场：作废可能在途的接收（正常收尾无在途，
				 * flush 无害；写失败路径必须作废，否则残留
				 * 完成位会被 ST_HDR 的头接收误读） */
				usb_ep1_rx_abort();
				/* flush 不在这里做（2026-11-20 教训：每条
				 * WRITE 都 flush = 每次清缓存，加速效果全毁，
				 * 还倒贴每次 ~1s 排空时间）。flush 移到任务级：
				 * 主机收尾时发 JBDL_FLUSH(7) 显式触发一次 */
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
