/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_KEY_H
#define _JUSTICEBL_KEY_H

/*
 * 底板 KEY 按键驱动 —— 纯输入，无长按逻辑（怎么用归上层，如
 * bootmode 的长按进 loader）。
 *
 * 硬件事实（kernel dts imx6ull-alientek-emmc.dts，证据优先级最高）：
 *   key-gpio = <&gpio1 18 GPIO_ACTIVE_LOW>
 *   = UART1_CTS_B 脚 ALT5 → GPIO1_IO18，按下=低电平；
 *   pad 配置 0xF080（dts 原值，含上拉——未按时读 1）。
 */

int key_init(void);		/* 门控+pad+GDIR 输入；0 成功 */

/* 1=按下（低电平），0=未按。连续采样归调用方（长按判定见 bootmode.c） */
int key_is_pressed(void);

#endif /* _JUSTICEBL_KEY_H */
