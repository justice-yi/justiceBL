/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_PRINTF_H
#define _JUSTICEBL_PRINTF_H

/*
 * 极简 printf（lib/printf.c）——格式化输出到调试串口 / 内存缓冲。
 *
 * 支持子集：
 *   转换：%%  %c  %s  %d/%i  %u  %x/%X  %p
 *   修饰：'-' 左对齐、'0' 零填充、十进制宽度（如 %08x、%-6s、%5d）
 *   长度：l（arm32 上 32 位）、ll（64 位）；h/hh 吃掉后按 int 处理
 * 不支持：%f 浮点、'*' 动态宽度、精度（%.3s）、%n（安全雷，永不支持）
 *
 * '\n' 在本模块出口翻译为 "\r\n"（终端行规程职责放这层，
 * uart 驱动保持纯字节设备）。返回值为逻辑字符数（'\r' 不计入）。
 */

#include <types.h>
#include <stdarg.h>

int printf(const char *fmt, ...);
int vprintf(const char *fmt, va_list ap);
int snprintf(char *buf, u32 size, const char *fmt, ...);

#endif /* _JUSTICEBL_PRINTF_H */
