// SPDX-License-Identifier: MIT
/*
 * 极简 printf 实现 —— 无 libc / 无 libgcc 依赖。
 *
 * 移植要点（为什么不直接用别人的）：
 *   1. stdarg.h 由编译器自带（属编译器运行时而非 libc），
 *      -nostdlib 下照常可用；ARM EABI 的 va_list 布局细节
 *      被 va_arg/va_start 完全屏蔽，本文件无需关心。
 *   2. 分两层：格式化核心 vformat() 只认 emit 回调，不直接摸
 *      UART——printf 换 UART 回调、snprintf 换缓冲回调，核心零
 *      改动换后端（Linux 内核 vsnprintf / U-Boot vsnprintf 同构）。
 *   3. 数制转换一律走 lib/math.c 的 udiv64_mod()：-nostdlib 下
 *      gcc 对 64 位 '/' 会拉 libgcc 的 __aeabi_uldivmod，链接不过
 *      （见 math.h 头注释，本项目用自带给法）。
 *   4. 不做 %f：浮点格式化需要数千行多精度十进制代码，bootloader
 *      场景用不上，砍掉（将来要也是接 tiny-printf 级别实现）。
 */

#include <types.h>
#include <stddef.h>
#include <stdarg.h>
#include <printf.h>
#include <uart.h>
#include <math.h>

/** emit 回调原型：吃一个字符（后端自己决定丢到 UART 还是缓冲）*/
typedef void (*emit_fn)(void *ctx, char c);

/* ---- 后端一：控制台（printf/vprintf 用）----
 * '\n' → "\r\n" 翻译放这层：串口终端要 CR+LF，而 uart 驱动是纯字节
 * 设备不该懂终端（Linux 在 tty 层、U-Boot 在 console 层做同一件事）*/
static void emit_console(void *ctx, char c)
{
	(void)ctx;
	if (c == '\n') {
		uart_putc('\r');
	}
	uart_putc(c);
}

/* ---- 后端二：内存缓冲（snprintf 用，C99 语义）----
 * 缓冲满后只计数不写入；返回值 = 假如缓冲足够大会写的长度 */
struct buf_ctx {
	char *buf;
	u32 size;		/* 含结尾 NUL 的总容量，可为 0 */
	u32 pos;
};

static void emit_buf(void *p, char c)
{
	struct buf_ctx *b = p;

	if (b->pos + 1 < b->size) {
		b->buf[b->pos] = c;
	}
	b->pos++;
}

/** 数字发射：val 按 base 输出；neg 前置 '-'；width/zeropad/left 组合 */
static void emit_num(emit_fn emit, void *ctx, u64 val, unsigned int base,
		     int upper, int neg, int width, int zeropad, int left,
		     int *count)
{
	const char *digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	char tmp[20];		/* 2^64-1 的十进制恰好 20 位 */
	int nd = 0, pad;

	do {
		u32 rem;

		val = udiv64_mod(val, base, &rem);
		tmp[nd++] = digs[rem];
	} while (val);
	pad = width - nd - neg;		/* 负宽度时下面循环自然不执行 */

	if (!left && zeropad && neg) {	/* %05d 的 '-' 要贴着数字 */
		emit(ctx, '-');
		(*count)++;
		neg = 0;
	}
	for (; !left && pad > 0; pad--) {
		emit(ctx, zeropad ? '0' : ' ');
		(*count)++;
	}
	if (neg) {
		emit(ctx, '-');
		(*count)++;
	}
	while (nd--) {
		emit(ctx, tmp[nd]);
		(*count)++;
	}
	for (; left && pad > 0; pad--) {
		emit(ctx, ' ');
		(*count)++;
	}
}

/** 字符串发射：width 右对齐（默认）或 '-' 左对齐；NULL 打 "(null)" */
static void emit_str(emit_fn emit, void *ctx, const char *s, int width,
		     int left, int *count)
{
	int len = 0, i;

	if (!s) {
		s = "(null)";
	}
	while (s[len]) {
		len++;
	}
	if (!left) {
		for (i = len; i < width; i++) {
			emit(ctx, ' ');
			(*count)++;
		}
	}
	for (i = 0; i < len; i++) {
		emit(ctx, s[i]);
		(*count)++;
	}
	if (left) {
		for (i = len; i < width; i++) {
			emit(ctx, ' ');
			(*count)++;
		}
	}
}

/**
 * vformat() - 格式化核心：解析 fmt，逐字符交给 emit 后端
 *
 * 返回发射的字符总数。所有静态辅助函数共享 (emit, ctx, count) 三元组
 * ——这就是"核心不认后端"的全部代价。
 */
static int vformat(emit_fn emit, void *ctx, const char *fmt, va_list ap)
{
	int count = 0;

	for (; *fmt; fmt++) {
		int zeropad = 0, left = 0, width = 0, longs = 0;

		if (*fmt != '%') {
			emit(ctx, *fmt);
			count++;
			continue;
		}
		fmt++;
		for (;; fmt++) {			/* flags */
			if (*fmt == '0') {
				zeropad = 1;
			} else if (*fmt == '-') {
				left = 1;
			} else {
				break;
			}
		}
		while (*fmt >= '0' && *fmt <= '9') {	/* width */
			width = width * 10 + (*fmt++ - '0');
		}
		while (*fmt == 'l' || *fmt == 'h') {	/* length */
			if (*fmt == 'l') {
				longs++;
			}
			fmt++;
		}

		switch (*fmt) {
		case 'c': {
			char cbuf[2] = { (char)va_arg(ap, int), '\0' };

			/* 借字符串路径让 %5c / %-5c 的宽度/对齐同样生效 */
			emit_str(emit, ctx, cbuf, width, left, &count);
			break;
		}
		case 's':
			emit_str(emit, ctx, va_arg(ap, const char *),
				 width, left, &count);
			break;
		case 'd':
		case 'i': {
			u64 v;
			int neg;

			/* 负数取绝对值走无符号路径；INT_MIN 用 u64 绕溢出 */
			if (longs >= 2) {
				long long sv = va_arg(ap, long long);

				neg = sv < 0;
				v = neg ? -(u64)sv : (u64)sv;
			} else {
				int sv = va_arg(ap, int);

				neg = sv < 0;
				v = neg ? -(u64)sv : (u64)sv;
			}
			emit_num(emit, ctx, v, 10, 0, neg, width, zeropad,
				 left, &count);
			break;
		}
		case 'u':
		case 'x':
		case 'X': {
			u64 v = (longs >= 2) ? va_arg(ap, unsigned long long)
					     : va_arg(ap, unsigned int);

			emit_num(emit, ctx, v, (*fmt == 'u') ? 10 : 16,
				 *fmt == 'X', 0, width, zeropad, left,
				 &count);
			break;
		}
		case 'p':		/* 统一 "0x" + 8 位十六进制 */
			emit(ctx, '0');
			emit(ctx, 'x');
			count += 2;
			emit_num(emit, ctx,
				 (u64)(unsigned long)va_arg(ap, void *),
				 16, 0, 0, 8, 1, 0, &count);
			break;
		case '%':
			emit(ctx, '%');
			count++;
			break;
		case '\0':		/* 格式串截断在 '%' 之后 */
			return count;
		default:		/* 不认得的转换：按字面吐出，可见可查 */
			emit(ctx, '%');
			emit(ctx, *fmt);
			count += 2;
			break;
		}
	}
	return count;
}

/**
 * printf() - 格式化输出到调试串口（无缓冲，轮询发送）
 * @fmt: 格式串，支持子集见 printf.h
 *
 * 返回逻辑字符数（'\n' 翻译出的 '\r' 不计入，glibc 同义）。
 */
int printf(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vformat(emit_console, NULL, fmt, ap);
	va_end(ap);
	return n;
}

/** vprintf() - printf 的 va_list 版（给上层包装日志前缀用）*/
int vprintf(const char *fmt, va_list ap)
{
	return vformat(emit_console, NULL, fmt, ap);
}

/**
 * snprintf() - 格化化到缓冲（C99 语义）
 * @buf: 目标缓冲；@size: 含 NUL 的容量（0 = 只算长度不写）
 *
 * 保证 NUL 结尾且绝不越界；返回值 = 假如缓冲足够大会写的长度。
 */
int snprintf(char *buf, u32 size, const char *fmt, ...)
{
	struct buf_ctx b = { buf, size, 0 };
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vformat(emit_buf, &b, fmt, ap);
	va_end(ap);
	if (size) {
		buf[b.pos < size - 1 ? b.pos : size - 1] = '\0';
	}
	return n;
}
