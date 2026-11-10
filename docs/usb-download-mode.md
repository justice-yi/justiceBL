# justiceBL USB 下载模式（v0.13）

> 触发：Linux `reboot loader` → SNVS LPGPR(0x20CC068) = 0x4C4F4144("LOAD") →
> WDOG 复位 → justiceBL 白名单消费（读后清零）→ 枚举为自定义名字的 USB 设备。
> 为将来 USB 烧录协议打底。**全部按 RM 软件配置流程实现，未参考 U-Boot。**

## 模块地图（耦合规则）

| 文件 | 职责 | 允许知道什么 |
|---|---|---|
| `include/usb_config.h` | 设备身份（VID/PID/名字）——**用户唯一改动点** | — |
| `drivers/usb/usb_ch9.c/.h` | 描述符 + 标准请求解码 | 只有协议（USB 2.0 ch9），零硬件知识 |
| `drivers/usb/usbd.c` | 引擎：dQH/dTD、EP0 状态机、轮询环 | ChipIdea IP 寄存器偏移；硬件经 usbd_soc 接口 |
| `drivers/usb/usbd_soc.c/.h` | SoC 专属：PLL3/CCGR6/USBPHY/USBNC/基址 | 全仓库唯一出现 i.MX 地址的 USB 文件 |
| `main.c` | 魔数白名单消费 + 入口调用 | 只认 `usbd_download_mode()` |

## RM 依据（IMX6ULL参考手册 Rev.1 11/2017）

| 步骤 | 章节 | 内容 |
|---|---|---|
| 控制器基址/间距 | 56.5 | OTG1=0x0218_4000，核间距 512 |
| 设备初始化五步 | 56.4.6.1 | USBMODE→dQH→ENDPTLISTADDR→(中断→本实现改轮询)→RS=1；EP0 免配 ENDPTCTRL（复位值 0x0080_0080 已使能）、免初始 prime |
| dQH | 56.4.5.1 | 48B/64 对齐；偶下标=RX/OUT（含 8B setup 缓冲），奇=TX/IN；MPL[26:16]/IOS(15)/ZLT(29)/MULT[31:30] |
| dTD | 56.4.5.2 | next[31:5]+T(0)、TotalBytes[30:16]、Status[7:0]（ACTIVE=bit7）、5 页缓冲 |
| EP0 流程 | **56.4.6.4.2** | SLOM=1 + SUTW tripwire 六步读 setup；data 阶段 prime 后查 ENDPTPRIME==0 && ENDPTSTAT==1；status 阶段 total=0 dTD；协议 stall 双向成对、下一 setup 自动清 |
| 寄存器偏移 | 56.6.x | USBCMD 0x140(RS=0/RST=1/SUTW=13)、USBSTS 0x144(UI=0/UEI=1/PCI=2/URI=6, W1C)、DEVICEADDR 0x154(USBADR[31:25]/USBADRA 24 暂存地址)、ENDPTLISTADDR 0x158(64 对齐)、**USBMODE 0x1A8(位图 SDIS=4/SLOM=3/ES=2/CM[1:0]——CM 在 bit[1:0]，设备=裸 0x2；写成 0x2<<2 会让 CM=idle，实测踩坑：D+ 永不上拉)、复位后只能写一次**、ENDPTSETUPSTAT 0x1AC(W1C)、ENDPTPRIME 0x1B0、ENDPTFLUSH 0x1B4、ENDPTSTAT 0x1B8、ENDPTCOMPLETE 0x1BC(ETCE[23:16]/ERCE[7:0], W1C)、ENDPTCTRL0 0x1C0 |
| PLL3 | 18.7.2 | CCM_ANALOG_PLL_USB1 @0x020C_8010：**EN_USB_CLKS(6)——480MHz 到 PHY 的输出使能（bit13 只送 CCM，漏了 bit6 则 PHY 无时钟、D+ 永不上拉，实测踩坑）** + POWER(12)/ENABLE(13) 置位、BYPASS(16) 清零，SET/CLR 寄存器写法 |
| 充电检测 | 57.2.7/57.4.2 | USB1_CHRG_DETECT @0x020C_81B0 写 EN_B(20)\|CHK_CHRG_B(19) 全关——不关则 DP 信号质量差 |
| PHY 补充 | 57.3.4 | ENUTMILEVEL2(14)\|3(15) 置位（FS/LS 设备）；SFTRST 置位→udelay→CLKGATE\|SFTRST 同清→udelay；PWD 清零 |
| 门控 | 18.6.29 | CCGR6 @0x020C_4080 CG0=usboh3（复位全开，代码仍显式置位） |
| PHY | 57.3 | USBPHY1 @0x020C_9000：CTRL(0x30) 清 CLKGATE(30)→SFTRST(31) 1→0；PWD(0x00) 清零（复位 001E_1C00 全断电） |
| 过流 | 56.5.1 | USBNC_USB_OTG1_CTRL @0x0218_4800 bit7 OVER_CUR_DIS=1（板上无 OC 采样） |

## 与 reboot-mode 框架的协议（两侧魔数必须一致）

| 侧 | 定义位置 | 值 |
|---|---|---|
| Linux DT | `imx6ull-alientek-emmc.dts` `&snvs/reboot-mode` | mode-normal=0x4E4F524D / mode-loader=0x4C4F4144 / mode-recovery=0x52434F56 |
| justiceBL | `include/usbd.h` | REBOOT_MAGIC_LOADER/RECOVERY 同值 |
| 消费规则 | 接力（谁消费谁清零） | LOAD：justiceBL 清零并进 USB；RCOV：justiceBL 放行 → U-Boot boot.scr（待实现）；NORM/0/垃圾：放行 |

## 看门狗拆弹（main.c，实测踩坑 2026-11-08）

Linux `imx2_wdt` 的 restart 处理器写 `WCR=WDE|WDA|WT=0`（0.5s 最短超时）触发本次复位；
**WDOG 超时型复位不清 WCR**（WDE 写一次不可清）→ justiceBL 继承一个 0.5s 定时炸弹：
每 0.5s 被复位一次，Windows 枚举窗口直接被炸掉（症状：`reboot loader` 后串口进 USB 模式、
约半秒后自发复位跳回正常启动、Windows 无反应）。

拆弹：main.c 开机读 WDOG1 WCR(0x020BC000)，**仅当 WDE(bit2) 已置位**才改写
WT=0xFF（0.5s→128s），WDT/SRE 位保留；WDE=0（正常上电）时绝不动 WCR——
WDE 写一次置位特性，正常路径不能顺手把看门狗点着。
诊断口诀：U-Boot 里 `md.w 0x020bc000`，bit2=1 即武装。

## 自发 POR 根因（2026-11-09 独立调查定案）

**症状**：USB 枚举完成后 ~1-2s 板子自发复位（SRSR=0x1 ipp_reset_b），无主机也复现；U-Boot fastboot 同板同口同线却稳定。

**根因**：USB PHY 模拟电源 VDD_USB_CAP 由片内 3P0 LDO（PMU_REG_3P0）供电，四方软件（ROM/U-Boot/Linux/justiceBL）都停留在其**复位默认态**：ENABLE_LINREG=0（旁路）、ENABLE_ILIMIT=1（限流）、自保模式——RM 39.4 明言该态带不动负载，要求软件切正常稳压，无人做。芯片状态位自证：OK_VDD3P0=0、BO_VDD3P0=1。**WDOG 复位不清 ANATOP/PMU**：Linux 拉起过的 usbotg2/usbphy2/PLL_USB2 遗留负载仍在（U-Boot fastboot 的 fresh 链只有 PHY1+PLL1），叠加后边际轨在枚举负载下塌陷 → 内部电源监督（或核心板 NCP803@2.93V 监视 DC3V3）→ ipp_reset_b。

**修复**（usbd_soc.c）：① `usbd_soc_phy_init` 开头把 3P0 切正常稳压（SET 寄存器写 EN_LINREG|EN_BO，稳压建立后 CLR 寄存器清 EN_ILIMIT）；② `usbd_soc_clock_init` 末尾断 PLL_USB2 模拟芯（CLR POWER）回到等负载；③ 探针纠错：真 REG_2P5=0x130（原 0x124 实为 3P0_SET）、补 MISC1/PLL2/PHY2 读回。**勘误**：WDOG 复位会清 WCR（此前注释有误，defuse 保留为兜底）；勘误手册无相关条目。

## 验收记录

- [ ] 烧 justiceBL.imx（1KB 偏移）→ U-Boot `mw.l 0x20cc068 0x4C4F4144; reset`
- [ ] justiceBL 串口打印 `reboot flag: loader caught, cleared` + `usbd: download mode, VID:PID=1209:0001 product="justiceBL Loader"`
- [ ] Windows 接 **OTG 口**（micro USB）→ 设备管理器出现 `justiceBL Loader`
- [ ] 不写魔数 reset → 正常启动 U-Boot（回归）

## 已知边界

轮询非中断；无 SDP 协议（不被 uuu 认，烧录协议自研）；无挂起/远程唤醒处理；EP1 bulk 仅在描述符与 ENDPTCTRL 层就绪，无传输处理；`dtd` 池每方向单描述符（控制传输足够，bulk 协议设计时扩展）。
