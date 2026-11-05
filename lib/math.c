// SPDX-License-Identifier: MIT
/*
 * 无 libgcc 的除法实现（移位减法）。见 math.h 头注释。
 */

#include <types.h>
#include <math.h>

#define BIT_ULL(nr)		(1ULL << (nr))

u32 udiv32(u32 n, u32 d)
{
	u32 q = 0;
	u32 bit = 1;

	while ((d < n) && !(d & 0x80000000U)) {
		d <<= 1;
		bit <<= 1;
	}
	while (bit) {
		if (n >= d) {
			n -= d;
			q |= bit;
		}
		d >>= 1;
		bit >>= 1;
	}
	return q;
}

/** n / d -> 商；*rem 带回余数（rem 可为 NULL）*/
u64 udiv64_mod(u64 n, u32 d, u32 *rem)
{
	u64 q = 0;
	int bit = 0;
	u64 dd = d;

	if (!d) {
		if (rem) {
			*rem = 0;
		}
		return 0;
	}
	while (dd < n && !(dd & BIT_ULL(63))) {
		dd <<= 1;
		bit++;
	}
	for (; bit >= 0; bit--) {
		if (n >= dd) {
			n -= dd;
			q |= 1ULL << bit;
		}
		dd >>= 1;
	}
	if (rem) {
		*rem = (u32)n;
	}
	return q;
}
