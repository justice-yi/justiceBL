// SPDX-License-Identifier: MIT
/*
 * CRC-32（反射 0xEDB88320）—— 无表逐位实现，体积极小，速度对本项目足够。
 */

#include <types.h>
#include <crc32.h>

u32 crc32_update(u32 crc, const void *data, u32 len)
{
	const u8 *p = data;

	while (len--) {
		crc ^= *p++;
		for (int i = 0; i < 8; i++) {
			crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1)));
		}
	}
	return crc;
}

u32 crc32(const void *data, u32 len)
{
	return crc32_update(0xFFFFFFFFU, data, len) ^ 0xFFFFFFFFU;
}
