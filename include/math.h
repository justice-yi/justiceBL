/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_MATH_H
#define _JUSTICEBL_MATH_H

/*
 * 无 libgcc 的除法支持 —— 裸机 -nostdlib 下 gcc 的 '/' 会引入
 * __aeabi_uidiv/libgcc，而其除零路径又依赖 raise()（libc），
 * 故自带移位减法实现（内核 do_div 同思路）。
 * 除数为 0 无定义，调用方保证非零。
 */

#include <types.h>

u32 udiv32(u32 n, u32 d);
u64 udiv64_mod(u64 n, u32 d, u32 *rem);

#endif /* _JUSTICEBL_MATH_H */
