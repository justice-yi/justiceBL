/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_TYPES_H
#define _JUSTICEBL_TYPES_H

/* 定宽类型与位宏 —— 与 Linux 内核同款（include/linux/types.h / bits.h 裁剪） */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define BIT(nr)			(1U << (nr))

#endif /* _JUSTICEBL_TYPES_H */
