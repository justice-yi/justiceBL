/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_USBD_H
#define _JUSTICEBL_USBD_H

/*
 * USB 下载模式公共接口 —— main.c 是唯一调用方。
 *
 * 触发协议（与 Linux 侧 syscon-reboot-mode 的 DT 词表互为约定，
 * 两边魔数必须一致，见 doc/usb-download-mode.md）：
 *   Linux:  syscall(RESTART2, "loader") -> SNVS_LPGPR = 0x4C4F4144
 *   本侧:   读 SNVS LPGPR，白名单命中才进本模式，消费后清零
 */

#include <types.h>

/* SNVS LPGPR 里的魔数（ascii: "LOAD" / "RCOV"） */
#define REBOOT_MAGIC_LOADER	0x4C4F4144
#define REBOOT_MAGIC_RECOVERY	0x52434F56

/* 进入 USB 下载模式：初始化并永久轮询，不返回 */
void usbd_download_mode(void) __attribute__((noreturn));

#endif /* _JUSTICEBL_USBD_H */
