/* SPDX-License-Identifier: MIT */
#ifndef _JUSTICEBL_DOWNLOAD_H
#define _JUSTICEBL_DOWNLOAD_H

/*
 * USB 下载协议（JBDL v1）公共接口 —— usbd.c 调用。
 * 协议内容/状态机见 download.c 文件头。
 */

/* 初始化下载会话：识别启动介质（烧写目标）并初始化它 */
void download_media_init(void);

/* 主循环轮询钩子：非阻塞，驱动协议状态机 */
void download_poll(void);

#endif /* _JUSTICEBL_DOWNLOAD_H */
