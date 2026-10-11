/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_LED_H
#define _JUSTICEBL_LED_H

/*
 * LED0 驱动 API：核心板 LED0（红色），低电平点亮。
 */

void led_init(void);
void led_set(int on);

#endif /* _JUSTICEBL_LED_H */
