/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_CRC32_H
#define _JUSTICEBL_CRC32_H

#include <types.h>

/* CRC-32（反射多项式 0xEDB88320，同 zlib/以太网），逐位实现免查表 */

u32 crc32_update(u32 crc, const void *data, u32 len);

/** 单次便捷接口（内部从 0xFFFFFFFF 起算，返回最终异或后的值） */
u32 crc32(const void *data, u32 len);

#endif /* _JUSTICEBL_CRC32_H */
