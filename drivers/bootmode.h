/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_BOOTMODE_H
#define _JUSTICEBL_BOOTMODE_H

/*
 * 消费 reboot-mode 魔数并分流：命中 loader 魔数 -> 进入 USB 下载模式
 * （不返回）；否则返回，继续正常启动。
 */
void bootmode_dispatch(void);

#endif /* _JUSTICEBL_BOOTMODE_H */
