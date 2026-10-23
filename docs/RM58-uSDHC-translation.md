# RM 第 58 章 uSDHC 原文翻译（分批进行）

> 译源：i.MX 6ULL Applications Processor Reference Manual, Rev. 1, 11/2017，第 58 章
> （NXP 原版 PDF，`07、I.MX6U参考资料/02、I.MX6ULL芯片资料/IMX6ULL参考手册.pdf`）。
> 体例：**逐句翻译**，不压缩不改写；图以 ASCII 复现；`译注`单独标出。
> 进度：**批次 1 = 58.1 / 58.1.1 / 58.1.2 / 58.3**。后续批次：58.2 信号表 →
> 58.4 功能描述 → 58.5 初始化与应用 → 58.6 命令与响应格式 → 58.7 软件限制 →
> 58.8+ 寄存器逐位定义（按需点名翻译）。

## 58.1 概述

【原文】The Ultra Secured Digital Host Controller (uSDHC) provides the interface
between the host system and the SD/SDIO/MMC cards, as depicted in Figure 58-1.

【译文】Ultra Secured Digital Host Controller（uSDHC，超安全数字主机控制器）
在主机系统与 SD/SDIO/MMC 卡之间提供接口，如图 58-1 所示。

【原文】The uSDHC acts as a bridge, passing host bus transactions to the
SD/SDIO/MMC cards by sending commands and performing data accesses to/from the
cards. It handles the SD/SDIO/MMC protocols at the transmission level.

【译文】uSDHC 充当一座桥：通过向卡发送命令并执行对卡的读/写数据访问，把主机
总线事务传递给 SD/SDIO/MMC 卡。它在传输级（transmission level）处理
SD/SDIO/MMC 协议。

【原文】The following are brief descriptions of the cards supported by the uSDHC:

【译文】以下是对 uSDHC 所支持各类卡的简要描述：

【原文】The Multi Media Card (MMC) is a universal low cost data storage and
communication media designed to cover a wide array of applications including mobile
video and gaming. Previous MMC cards were based on a 7-pin serial bus with a single
data pin, while the new high speed MMC communication is based on an advanced 11-pin
serial bus designed to operate in the low voltage range.

【译文】多媒体卡（MMC）是一种通用的低成本数据存储与通信介质，设计用于覆盖包括
移动视频与游戏在内的广泛应用。早期的 MMC 卡基于 7 脚串行总线、只有一根数据线；
而新的高速 MMC 通信则基于先进的 11 脚串行总线，设计工作于低电压范围。

【原文】The Secure Digital Card (SD) is an evolution of the old MMC technology. It
is specifically designed to meet the security, capacity, performance, and
environment requirements inherent in newly-emerging audio and video consumer
electronic devices. The physical form factor, pin assignment and data transfer
protocol are forward-compatible with the old MMC (with some additions).

【译文】安全数字卡（SD）是老 MMC 技术的演进。它专为满足新兴消费类音视频电子
设备在安全性、容量、性能与环境方面固有的需求而设计。其物理外形尺寸、引脚分配
与数据传输协议对老 MMC 前向兼容（另有若干增补）。

【原文】Under the SD protocol, it can be categorized into Memory card, I/O card
and Combo card, which has both memory and I/O functions. The memory card invokes a
copyright protection mechanism that complies with the security of the SDMI
standard. The I/O card, which is also known as SDIO card, provides high-speed data
I/O with low power consumption for mobile electronic devices. For the sake of
simplicity, the following figure does not show cards with reduced size or mini
cards.

【译文】在 SD 协议之下，可分类为存储卡（Memory card）、I/O 卡与 Combo 卡（同时
具备存储与 I/O 功能）。存储卡调用了符合 SDMI 标准安全性的版权保护机制。I/O 卡
又称 SDIO 卡，为移动电子设备提供低功耗的高速数据 I/O。为简洁起见，下图未画出
缩小尺寸卡（reduced size）与 mini 卡。

```
【图 58-1 复现：uSDHC 与卡的连接关系】
            ┌────────────────────────────────────┐
 主机系统 ───┤ IP Bus      ┌───────────────┐      │      ┌──────────┐
 (Host      │  AHB Bus ───►  寄存器堆/      │ Trans-│─CMD──►  卡槽    │──► MMC 卡
  System)   │  DMA 接口    │  DMA/DATA BUF │ ceiver│─CLK──► (Card   │    SD 卡
            │             └───────────────┘(收发器)│─DAT──►  Slot)  │    SDIO 卡
            └────────────────────────────────────┘      └──────────┘
```

## 58.1.1 特性

【原文】The features of the uSDHC module include the following:

【译文】uSDHC 模块的特性包括：

- 【原文】Conforms to the SD Host Controller Standard Specification version 3.0
- 【译文】符合 SD 主机控制器标准规范 3.0 版
- 【原文】Compatible with the MMC System Specification version 4.2/4.3/4.4/4.41/4.5
- 【译文】兼容 MMC 系统规范 4.2/4.3/4.4/4.41/4.5 版
- 【原文】Compatible with the SD Memory Card Specification version 3.0 and
  supports the Extended Capacity SD Memory Card
- 【译文】兼容 SD 存储卡规范 3.0 版，并支持扩展容量 SD 存储卡（SDXC）
- 【原文】Compatible with the SDIO Card Specification version 3.0
- 【译文】兼容 SDIO 卡规范 3.0 版
- 【原文】Designed to work with SD Memory, miniSD Memory, SDIO, miniSDIO, SD
  Combo, MMC, MMC plus, and MMC RS cards
- 【译文】设计用于 SD 存储卡、miniSD 存储卡、SDIO、miniSDIO、SD Combo、MMC、
  MMC plus 与 MMC RS 卡
- 【原文】Card bus clock frequency up to 208 MHz
- 【译文】卡总线时钟频率最高 208 MHz
- 【原文】Supports 1-bit / 4-bit SD and SDIO modes, 1-bit / 4-bit / 8-bit MMC modes
- 【译文】支持 SD 与 SDIO 的 1 位/4 位模式，MMC 的 1 位/4 位/8 位模式
- 【原文】Up to 832 Mbps of data transfer for SDIO cards using 4 parallel data
  lines in SDR(Single Data Rate) mode
- 【译文】SDIO 卡在 SDR（单倍数据速率）模式下使用 4 根并行数据线，数据传输
  最高 832 Mbps
- 【原文】Up to 400 Mbps of data transfer for SDIO card using 4 parallel data
  lines in DDR (Double Data Rate) mode
- 【译文】SDIO 卡在 DDR（双倍数据速率）模式下使用 4 根并行数据线，数据传输
  最高 400 Mbps
- 【原文】Up to 832 Mbps of data transfer for SDXC cards using 4 parallel data
  lines in SDR mode / Up to 400 Mbps … in DDR mode
- 【译文】SDXC 卡在 SDR 模式 4 线最高 832 Mbps，DDR 模式最高 400 Mbps
- 【原文】Up to 416 Mbps of data transfer for MMC cards using 8 parallel data
  lines in SDR mode / Up to 832 Mbps … in DDR mode
- 【译文】MMC 卡在 SDR 模式 8 线最高 416 Mbps，DDR 模式最高 832 Mbps
- 【原文】Supports single block/multi-block read and write
- 【译文】支持单块/多块读与写
- 【原文】Supports block sizes of 1 ~ 4096 bytes
- 【译文】支持的块大小为 1~4096 字节
- 【原文】Supports the write protection switch for write operations
- 【译文】写操作支持写保护开关
- 【原文】Supports both synchronous and asynchronous abort
- 【译文】支持同步与异步中止
- 【原文】Supports pause during the data transfer at block gap
- 【译文】支持在数据传输期间于块间隙（block gap）暂停
- 【原文】Supports SDIO Read Wait and Suspend Resume operations
- 【译文】支持 SDIO Read Wait 与 Suspend/Resume 操作
- 【原文】Supports Auto CMD12 for multi-block transfer
- 【译文】多块传输支持 Auto CMD12（自动结束命令）
- 【原文】Host can initiate non-data transfer command while data transfer is
  in progress
- 【译文】数据传输进行中，主机仍可发起不带数据的命令
- 【原文】Allows cards to interrupt the host in 1-bit and 4-bit SDIO modes,
  also supports interrupt period
- 【译文】允许卡在 1 位与 4 位 SDIO 模式下中断主机，并支持中断周期
- 【原文】Embodies a fully configurable 128x32-bit FIFO for read/write data
- 【译文】内置完全可配置的 128×32 位读写数据 FIFO
- 【原文】Supports internal and external DMA capabilities
- 【译文】支持内部与外部 DMA 能力
- 【原文】Support voltage selection by configuring vendor specific register bit
- 【译文】支持通过配置厂商专用寄存器位来选择电压
- 【原文】Supports Advanced DMA to perform linked memory access
- 【译文】支持高级 DMA（ADMA）以执行链式内存访问

## 58.1.2 模式与操作

### 58.1.2.1 数据传输模式

【原文】The uSDHC can select the following modes for data transfer:

【译文】uSDHC 可为数据传输选择以下模式：

| 模式（原文） | 译文 |
|---|---|
| SD 1-bit / SD 4-bit | SD 1 位 / SD 4 位 |
| MMC 1-bit / 4-bit / 8-bit | MMC 1 位 / 4 位 / 8 位 |
| Identification Mode (up to 400 kHz) | 识别模式（最高 400 kHz） |
| MMC full speed mode (up to 26 MHz) | MMC 全速模式（最高 26 MHz） |
| MMC high speed mode (up to 52 MHz) | MMC 高速模式（最高 52 MHz） |
| MMC HS200 mode (up to 200 MHz) | MMC HS200 模式（最高 200 MHz） |
| MMC DDR mode (52 MHz both edges) | MMC DDR 模式（52 MHz 双沿） |
| SD/SDIO full speed mode (up to 25 MHz) | SD/SDIO 全速模式（最高 25 MHz） |
| SD/SDIO high speed mode (up to 50 MHz) | SD/SDIO 高速模式（最高 50 MHz） |
| SD/SDIO UHS-I mode (up to 208 MHz in SDR, up to 50 MHz in DDR) | SD/SDIO UHS-I 模式（SDR 最高 208 MHz，DDR 最高 50 MHz） |

## 58.3 时钟

【原文】The table found here describes the clock sources for uSDHC. Please see
Clock Controller Module (CCM) for clock setting, configuration and gating
information.

【译文】下表描述 uSDHC 的时钟源。时钟的设置、配置与门控信息请参见时钟控制
模块（CCM）章节。

| 时钟名（原文） | 时钟根 | 说明（原文意译） |
|---|---|---|
| hclk | ahb_clk_root | AHB 总线时钟 |
| ipg_clk | ipg_clk_root | 外设时钟 |
| ipg_clk_perclk | usdhc_clk_root | 基准时钟（Base clock） |
| ipg_clk_s | ipg_clk_root | 用于寄存器访问的外设访问时钟 |

> 译注：58.2 外部信号（引脚清单表）篇幅较长，作为批次 2；本文件后续批次逐次追加。

## 58.2 外部信号（批次 2）

【原文】The following table describes the external signals of USDHC:

【译文】下表描述 USDHC 的外部信号：

**表 58-1. USDHC 外部信号 —— USDHC1（SD1_*）部分**

| 信号 | 描述【译文】 | 焊盘（Pad） | 模式 | 方向 |
|---|---|---|---|---|
| SD1_CD_B | 卡检测引脚。若不使用（对板载存储器），拉低以指示有卡在位 | CSI_DATA05 / GPIO1_IO03 / UART1_RTS_B | ALT8 / ALT4 / ALT2 | I |
| SD1_CLK | MMC/SD/SDIO 卡的时钟 | SD1_CLK | ALT0 | O |
| SD1_CMD | 连接到卡的 CMD 线 | SD1_CMD | ALT0 | IO |
| SD1_DATA0 | 所有模式下的 DATA0 线。**亦用于检测 busy 状态** | SD1_DATA0 | ALT0 | IO |
| SD1_DATA1 | 4/8 位模式下的 DATA1 线。1/4 位模式下亦用于检测（卡）中断 | SD1_DATA1 | ALT0 | IO |
| SD1_DATA2 | DATA2 线，或 4 位模式下的 Read Wait；1 位模式下作 Read Wait | SD1_DATA2 | ALT0 | IO |
| SD1_DATA3 | 4/8 位模式下的 DATA3 线；1 位模式下可配置为卡检测引脚 | SD1_DATA3 | ALT0 | IO |
| SD1_DATA4 | 8 位模式下的 DATA4 线，其它模式不使用 | NAND_READY_B | ALT1 | IO |
| SD1_DATA5 | 8 位模式下的 DATA5 线 | NAND_CE0_B | ALT1 | IO |
| SD1_DATA6 | 8 位模式下的 DATA6 线 | NAND_CE1_B | ALT1 | IO |
| SD1_DATA7 | 8 位模式下的 DATA7 线 | NAND_CLE | ALT1 | IO |
| SD1_LCTL | LED 控制，用于驱动外部 LED，高有效，完全由驱动控制，可选输出 | ENET1_RX_DATA0 / GPIO1_IO04 / GPIO1_IO09 | ALT8 / ALT4 / ALT6 | O |
| SD1_RESET_B | 卡硬件复位信号，低有效 | CSI_DATA06 / NAND_WP_B | ALT8 / ALT1 | O |
| SD1_TESTER_TRIGGER | （测试触发） | UART1_RX_DATA | ALT7 | IO |
| SD1_VSELECT | IO 电源电压选择信号 | CSI_DATA07 / ENET1_RX_EN / GPIO1_IO05 | ALT8 / ALT8 / ALT4 | O |
| SD1_WP | 卡写保护检测。若不使用（板载存储器），拉低指示未写保护 | CSI_DATA04 / GPIO1_IO02 / UART1_CTS_B | ALT8 / ALT4 / ALT2 | I |

**表 58-1（续）—— USDHC2（SD2_*）部分**（描述文字与 SD1 逐字相同，仅焊盘不同，译从略、焊盘照录）

| 信号 | 焊盘候选 | 模式 |
|---|---|---|
| SD2_CD_B | CSI_MCLK / GPIO1_IO07 / UART1_RTS_B | ALT1 / ALT4 / ALT8 |
| SD2_WP | CSI_DATA04 / GPIO1_IO02 / UART1_CTS_B | ALT8 / ALT4 / ALT2 |
| SD2_CLK | CSI_VSYNC / LCD_DATA19 / NAND_RE_B | ALT1 / ALT8 / ALT1 |
| SD2_CMD | CSI_HSYNC / LCD_DATA18 / NAND_WE_B | ALT1 / ALT8 / ALT1 |
| SD2_DATA0 | CSI_DATA00 / LCD_DATA20 / NAND_DATA00 | ALT1 / ALT8 / ALT1 |
| SD2_DATA1 | CSI_DATA01 / LCD_DATA21 / NAND_DATA01 | ALT1 / ALT8 / ALT1 |
| SD2_DATA2 | CSI_DATA02 / LCD_DATA22 / NAND_DATA02 | ALT1 / ALT8 / ALT1 |
| SD2_DATA3~DATA7 | （在原书次页，批次 2b 补录——预期为 NAND_DATA03~07 等焊盘） | ALT1 |

> 译注：这张表正是我们驱动里 `sd1_pins[]`/`emmc_pins[]` 的原文出处——
> USDHC1 用 SD1_* 焊盘 ALT0，USDHC2 的 eMMC 走 NAND_* 焊盘 ALT1，
> 与 `imx6ul-pinfunc.h` 逐条对应。

### 58.2.1 信号概述

【原文】The uSDHC has 14 associated I/O signals.
• The CLK is an internally generated clock used to drive the MMC, SD, SDIO cards.
• The CMD I/O is used to send commands and receive responses to and from the
card. Eight data lines (DAT7~DAT0) are used to perform data transfers between
the uSDHC and the card.
• The CD and WP are card detection and write protection signals directly routed
from the socket. These two signals are active low (0). A low on CD# means that a
card is inserted, and a high on WP means that the write protect switch is active.
• LCTL is an output signal used to drive an external LED to indicate that the SD
interface is busy.
• RST is an output signal used to reset the MMC card.
• VSELECT is an output signal used to change the voltage of the external power
supplier.
CD, WP, LCTL, RST and VSELECT are all optional for system implementation. If the
uSDHC needs to support a 4-bit data transfer, DAT7~DAT4 can also be optional and
tied to high.

【译文】uSDHC 有 14 根关联 I/O 信号。
- CLK 是内部产生的时钟，用于驱动 MMC、SD、SDIO 卡。
- CMD（I/O）用于向卡发送命令并从卡接收响应。八根数据线（DAT7~DAT0）用于在
  uSDHC 与卡之间执行数据传输。
- CD 与 WP 是直接从卡座布线的卡检测与写保护信号。这两个信号低有效（0）。
  CD# 为低表示卡已插入；WP 为高表示写保护开关已激活。
- LCTL 是输出信号，用于驱动外部 LED 指示 SD 接口忙。
- RST 是输出信号，用于复位 MMC 卡。
- VSELECT 是输出信号，用于改变外部供电的电压。
CD、WP、LCTL、RST、VSELECT 在系统实现中均为可选。若 uSDHC 只需支持 4 位数据
传输，DAT7~DAT4 亦为可选，可拉高悬置。

**表 58-1（续完）—— SD2_DATA3~SD2_WP**（批次 2b）

| 信号 | 描述（与 SD1 对应行逐字相同，译从略） | 焊盘候选 | 模式 | 方向 |
|---|---|---|---|---|
| SD2_DATA3 | DATA3 线；1 位模式可作卡检测 | NAND_DATA02 / CSI_DATA03 / LCD_DATA23 | ALT1 / ALT1 / ALT8 | IO |
| SD2_DATA4 | 8 位模式 DATA4 线 | NAND_DATA03 / CSI_DATA04 / LCD_DATA14 | ALT1 | IO |
| SD2_DATA5 | 8 位模式 DATA5 线 | NAND_DATA04 / CSI_DATA05 / LCD_DATA15 | ALT1 / ALT1 / ALT8 | IO |
| SD2_DATA6 | 8 位模式 DATA6 线 | NAND_DATA06 / CSI_DATA06 / LCD_DATA16 | ALT1 / ALT1 / ALT8 | IO |
| SD2_DATA7 | 8 位模式 DATA7 线 | NAND_DATA07 / CSI_DATA07 / LCD_DATA17 | ALT1 / ALT1 / ALT8 | IO |
| SD2_LCTL | LED 控制（高有效，可选） | ENET1_RX_DATA1 | ALT8 | O |
| SD2_RESET_B | 卡硬件复位，低有效 | GPIO1_IO09 / LCD_DATA13 / NAND_ALE | ALT4 / ALT8 / ALT1 | O |
| SD2_TESTER_TRIGGER | 测试触发 | UART1_CTS_B | ALT7 | IO |
| SD2_VSELECT | IO 电源电压选择 | ENET1_TX_DATA0 / GPIO1_IO08 | ALT8 / ALT4 | O |
| SD2_WP | 写保护检测（描述同 SD1_WP） | 见原表次页 | — | I |

> 译注：PDF 文本抽取时此表列错位，上表按 SD1 同构规则复原；焊盘/模式以原书
> PDF 表格为准。

## 58.4 功能描述（批次 3 起）

【原文】The following sections provide a brief functional description of the major
system blocks, including the Data Buffer, DMA AHB interface, register bank as well
as IP Bus interface, dual-port memory wrapper, data/command controller, clock and
reset manager, and clock generator.

【译文】以下各节对主要系统块给出简要功能描述，包括：数据缓冲（Data Buffer）、
DMA AHB 接口、寄存器堆与 IP 总线接口、双端口存储器包装（dual-port memory
wrapper）、数据/命令控制器、时钟与复位管理器、时钟生成器。

### 58.4.1 数据缓冲（Data Buffer）

【原文】The uSDHC uses one configurable data buffer to transfer data between the
system bus (IP Bus or AHB Bus) and the SD card in an optimized manner, maximizing
throughput between the two clock domains (IP peripheral clock and the master
clock). The buffer is used as temporary storage for data being transferred between
the host system and the card. The watermark levels for read and write are both
configurable and can be from 1 to 128 words. The burst lengths for read and write
are also configurable and can be from 1 to 31 words.

【译文】uSDHC 使用一个可配置的数据缓冲，在系统总线（IP 总线或 AHB 总线）与
SD 卡之间以优化方式传输数据，最大化两个时钟域（IP 外设时钟与主时钟）之间的
吞吐。该缓冲用作主机系统与卡之间待传数据的临时存储。读与写的水位线
（watermark level）均可配置，范围为 1~128 字；读与写的突发长度（burst length）
亦可配置，范围为 1~31 字。

```
【图 58-3 复现：uSDHC Buffer 方案】
  ┌───────────────────── uSDHC ─────────────────────┐
  │ uSDHC 寄存器 │ Status Sync ──► uSDHC_irq        │
  │     ▲         ▲                                 │
  │ IP Bus I/F ► Buffer Control ► Buffer RAM        │──dma_req──►外部DMA
  │ AHB Bus ───► Internal DMA      Wrapper          │
  │                └──────────► Tx/Rx FIFO ◄──► Sync FIFOs ◄──► SD Bus I/F ──► 卡
  └─────────────────────────────────────────────────┘
```

【原文】There are 3 transfer modes to access the data buffer:

【译文】访问数据缓冲共有 3 种传输模式：

- **CPU 轮询模式**【原文】For a host read operation, when the number of words
  received in the buffer meets or exceeds the RD_WML watermark value, by polling
  the BRR bit, the Host Driver can read the Buffer Data Port register to fetch
  the amount of words set in the RD_WML register from the buffer. The write
  operation is similar.【译文】对主机读操作：当缓冲中收到的字数达到或超过
  RD_WML 水位值时，通过轮询 BRR 位，主机驱动可读 Buffer Data Port 寄存器、按
  RD_WML 设定的字数从缓冲取数。写操作与此类似。
- **外部 DMA 模式**【原文】For a read operation, when there are more words
  received in the buffer than the amount set in the RD_WML register, a DMA
  request is sent out to inform the external DMA to fetch the data. The request
  will be immediately de-asserted when there is an access on the Buffer Data
  Port register.【译文】对读操作：当缓冲收到的字数超过 RD_WML 设定值时，发出
  DMA 请求通知外部 DMA 取数；对 Buffer Data Port 寄存器的一次访问会立即撤销
  该请求。
- **内部 DMA 模式**：见 58.4.2 与 58.5.5（ADMA 操作）。

#### 58.4.1.1 写操作序列

【原文】There are three ways to write data into the buffer when the user transfers
data to the card: • External DMA through the uSDHC DMA request signal • Processor
core polling through the BWR bit in Interrupt Status register (interrupt or
polling) • Internal DMA.

【译文】用户向卡传输数据时，把数据写入缓冲有三种方式：经 uSDHC DMA 请求信号
的外部 DMA；处理器核经中断状态寄存器 BWR 位轮询（或中断）；内部 DMA。

【原文】When the internal DMA is not used, (the DMAEN bit in the Transfer Type
register is not set when the command is sent), the uSDHC asserts a DMA request
when the amount of buffer space exceeds the value set in the WR_WML register, and
is ready for receiving new data. At the same time, the uSDHC sets the BWR bit.
The buffer write ready interrupt will be generated if it is enabled by software.

【译文】不使用内部 DMA 时（发命令时 Transfer Type 寄存器的 DMAEN 位未置位）：
当缓冲空间超过 WR_WML 设定值、准备好接收新数据时，uSDHC 发出 DMA 请求；同时
置位 BWR 位。若软件使能，将产生 buffer write ready 中断。

【原文】When internal DMA is used, the uSDHC will not inform the system before all
the required number of bytes are transferred (if no error was encountered). When
an error occurs during the data transfer, the uSDHC will abort the data transfer
and abandon the current block. The Host Driver should read the contents of the
DMA System Address register to obtain the starting address of the abandoned data
block. If the current data transfer is in multi-block mode, the uSDHC will not
automatically send CMD12, even though the AC12EN bit in the Transfer Type
register is set. The Host Driver sends CMD12 in this scenario and re-starts the
write operation from that address. It is recommended that a Software Reset for
Data be applied before the transfer is re-started.

【译文】使用内部 DMA 时，在全部所需字节传完之前 uSDHC 不会通知系统（若无错误
发生）。数据传输中出现错误时，uSDHC 将中止传输并放弃当前块；主机驱动应读 DMA
System Address 寄存器的内容，取得被放弃数据块的起始地址。若当前传输为多块
模式，即使 Transfer Type 寄存器的 AC12EN 位已置位，uSDHC 也不会自动发
CMD12——此场景由主机驱动发 CMD12，并从该地址重启写操作。建议重启传输前先做
一次"数据软复位"（Software Reset for Data）。

【原文】The uSDHC will not start data transmission until the number of words set in
the WR_WML register can be held in the buffer. If the buffer is empty and the
Host System does not write data in time, the uSDHC will stop the CLK to avoid the
data buffer underrun situation.

【译文】直到缓冲能容纳 WR_WML 设定的字数之前，uSDHC 不会开始数据发送。若缓冲
已空而主机系统未及时写入数据，uSDHC 将停止 CLK，以避免数据缓冲下溢
（underrun）。

#### 58.4.1.2 读操作序列

【原文结构与 58.4.1.1 完全对称，逐段译文如下】

用户从卡传输数据时，从缓冲读出数据有三种方式：经 uSDHC DMA 请求信号的外部
DMA；处理器核经中断状态寄存器 BRR 位轮询（或中断）；内部 DMA。

不使用内部 DMA 时（发命令时 DMAEN 未置位）：当可用数据超过 RD_WML 设定值、
可供系统取数时，uSDHC 发出 DMA 请求；同时置位 BRR 位。若软件使能，将产生
buffer read ready 中断。

使用内部 DMA 时，在全部所需字节传完之前 uSDHC 不会通知系统（若无错误）。
出错时 uSDHC 中止传输并放弃当前块；主机驱动应读 DMA System Address 寄存器
取得被放弃块的起始地址。多块模式下即使 AC12EN 置位，uSDHC 也不会自动发
CMD12——由主机驱动发 CMD12 并从该地址重启读操作。建议重启前先做数据软复位。

【原文】For any write transfer mode, the uSDHC will not start data transmission
until the number of words set in the RD_WML register are in the buffer. If the
buffer is full and the Host System does not read data in time, the uSDHC will stop
the CLK to avoid the data buffer over-run situation.

【译文】对任何传输模式，直到缓冲中达到 RD_WML 设定的字数之前，uSDHC 不会开始
数据发送。若缓冲已满而主机系统未及时读取数据，uSDHC 将停止 CLK，以避免数据
缓冲上溢（over-run）。

> 译注：58.4.1.3 起为批次 3b。RM 原文在读序列末段写的是 "For any write
> transfer mode…RD_WML"——按上下文应为 read 之意，此处照原文直译并保留。

#### 58.4.1.3 数据缓冲与块大小

【原文】The user needs to know the buffer size for the buffer operation during a data
transfer to utilize it in the most optimized way. In the uSDHC, the only data buffer
can hold up to 128 words (32-bit) and the watermark levels for write and read can be
configured accordingly. For both read and write, the watermark level can be from 1 to
128 words. For both read and write the burst length can be from 1 to 31 words. The
Host Driver may configure the value according to the system situation and requirement.

【译文】用户需要了解数据传输中缓冲操作的缓冲区大小，以便以最优方式利用它。
uSDHC 中唯一的数据缓冲最多容纳 128 字（32 位），读写水位线可相应配置。读与
写的水位线均可在 1~128 字之间；读与写的突发长度均可在 1~31 字之间。主机驱动
可按系统状况与需求配置。

【原文】During a multi-block data transfer, the block length can be set to any value
between 1 and 4096 bytes, satisfying the requirements of the external card. The only
restriction is from the external card, which can be limited in size or support of a
partial block access (which is not the integer times of 512 bytes). As uSDHC treats
each block individually, for block sizes which are not multiples of four (not
word-aligned) stuffed bytes are required at the end of each block. For example, if
the block size is 7 bytes and there are 12 blocks to write, the system side must
write two times for each block. For each block the ending byte will be abandoned by
uSDHC because it only sends 7 bytes to the card and picks data from the following
system write, resulting in 24 beats of write access in total.

【译文】多块传输期间，块长可设为 1~4096 字节间任意值以满足外部卡要求。唯一的
限制来自外部卡（尺寸限制或是否支持非 512 字节整数倍的"部分块访问"）。由于
uSDHC 逐块处理，对非 4 字节整数倍（非字对齐）的块长，每块末尾需要填充字节。
例如块长 7 字节、写 12 块：系统侧每块必须写两次，每块末尾那个填充字节会被
uSDHC 丢弃（它只向卡发 7 字节、从下一次系统写中取数），合计 24 拍写访问。

#### 58.4.1.4 大传输的分割

【原文】This SDIO command CMD53 definition limits the maximum data size of data
transfers according to the following formula: Max data size = Block size × Block
count. The length of a multiple block transfer needs to be in block size units. If
the total data length can't be divided evenly into a multiple of the block size,
then there are two ways to transfer the data which depend on the function and the
card design. Option 1 is for the Host Driver to split the transaction. The remainder
of the block size data is then transferred by using a single block command at the
end. Option 2 is to add dummy data in the last block to fill the block size. For
option 2, the card must manage the removal of the dummy data.

【译文】SDIO 命令 CMD53 的定义按公式"最大数据尺寸 = 块长 × 块数"限制传输
上限。多块传输的长度必须以块长为单位。若总长不能被块长整除，有两种传法
（取决于功能与卡设计）：方案一由主机驱动拆分事务，块长余数部分在最后用
单块命令传输；方案二在最后一块补哑数据凑满块长，卡方须负责剔除哑数据。

```
【图 58-6 复现：大传输分割示例】
  544 字节 WLAN 帧 = 802.11 MAC 头 + IV + 帧体 + ICV + FCS
       │ 按 64 字节块切分（卡只支持 64 字节块）
       ▼
  [64B 块#1][64B 块#2]…[64B 块#8][余 32 字节]
       │ 块传输模式(CMD53)              字节传输模式(CMD53)
       ▼                                 ▼
  发块 #1~#8 ──────────────────────► 再发 32 字节尾巴
```

#### 58.4.1.5 外部 DMA 请求

【原文】When the internal DMA is not in use and external DMA is enabled, the Data
Buffer will generate a DMA request to the system. During a write operation, when
the number of WR_WML words can be held in the buffer free space, the signal
uSDHC_dreq_b is asserted to 0, informing the Host System of a DMA write. The BWR
bit in the Interrupt Status register is also set, as long as the BWRSEN bit in the
Interrupt Status Enable register is set. … On read operation, when the number of
RD_WML words are already in the buffer, the signal uSDHC_dreq_b is asserted to 0,
informing the Host System for a DMA read. …

【译文】不使用内部 DMA 且使能外部 DMA 时，数据缓冲会向系统发 DMA 请求。
写操作：当缓冲空闲空间能容纳 WR_WML 字时，uSDHC_dreq_b 信号拉 0，通知主机
系统做 DMA 写；同时（只要 INT_STATUS_EN 的 BWRSEN 置位）中断状态寄存器的
BWR 位置位。读操作：当缓冲中已有 RD_WML 字时，uSDHC_dreq_b 拉 0 通知 DMA 读，
BRR 位同理置位。（两种请求都在对 Data Port 若干次访问、缓冲不再满足水位条件
后撤销。）

【原文】If the DMA burst length can't change during a data transfer for an external
DMA transfer, the watermark level (read or write) must be a divisor of the block
size. If it is not, transferring the block may cause buffer under-run (read) or
over-run (write). For example, if the block size is 512 bytes, the watermark level
of read (or write) must be a power of two between 1 and 128. For processor core
polling access there is no such issue, as the last access in the block transfer can
be controlled by software. The watermark level can be any value, even larger than
the block size (but no greater than 128 words)…

【译文】外部 DMA 若传输期间突发长度不可变，则水位线必须能整除块长，否则传块
可能导致下溢（读）或上溢（写）。例如块长 512 字节时，水位线必须是 1~128 间
的 2 的幂。**处理器核轮询访问无此问题**——块传输的最后一次访问由软件控制，
水位线可取任意值、甚至大于块长（但不超过 128 字）……

【原文】The uSDHC also supports non-word aligned block size … For the CPU polling
access, the burst length can be 1 to 128 words, without restriction. … even though
8 words are transferred via the Data Port register, the uSDHC will transfer only 31
bytes over the SD Bus, as required by the BLKSIZE bits. In this data transfer, with
non-word aligned block size, the endian mode should be set cautiously or invalid
data will be transferred to and from the card.

【译文】uSDHC 也支持非字对齐块长（前提是卡支持）。CPU 轮询访问的突发长度可
为 1~128 字无限制……例如经 Data Port 传了 8 个字，uSDHC 仍只按 BLKSIZE 位的
要求在 SD 总线上发 31 字节。**非字对齐块长的传输中，端序模式必须谨慎设置，
否则卡与主机间会传出无效数据。**

> 译注：RM 在此明说了端序模式的重要性——我们的 bswap32 一课在原文的预言内。

### 58.4.2 DMA AHB 接口

【原文】The internal DMA implements a DMA engine and the AHB master. When the
internal DMA is enabled, the uSDHC_dreq_b will not be asserted during the transfer,
but the BWR and BRR bits will be set if the BWRSEN and BRRSEN bits have been set in
the Interrupt Status Enable register.

【译文】内部 DMA 实现了 DMA 引擎与 AHB 主设备。内部 DMA 使能时，传输期间
uSDHC_dreq_b 不再置位；但只要 INT_STATUS_EN 里 BWRSEN/BRRSEN 置位，BWR/BRR
位仍会置位。

```
【图 58-7 复现：DMA AHB 接口框图】
   uSDHC 寄存器 ◄──► AHB Interface Master Logic ──► AHB 信号簇
        ▲                  │系统地址/读写指示/错误指示        │
        │            DMA Engine ◄── Burst Length            ▼
   Data Port ◄── Buffer Control ──► DMA Request        （AHB 总线→内存）
```

#### 58.4.2.1 内部 DMA 请求

【原文】If the watermark level requirement is met in data transfer or if the last
data of current block is ready in the data buffer, and the Internal DMA is enabled,
the Data Buffer block will send a DMA request to AHB interface. Meanwhile, the
external DMA request signal (uSDHC_dreq_b) is disabled. The delay in response from
the internal DMA engine depends on the system AHB bus loading and the priority
assigned to the uSDHC. The DMA engine does not respond to the request during its
burst transfer, but is ready to serve as soon as the burst is over. …

【译文】数据传输满足水位条件、或当前块最后数据已在缓冲，且内部 DMA 使能时，
数据缓冲块向 AHB 接口发 DMA 请求；同时外部 DMA 请求信号（uSDHC_dreq_b）被
禁用。内部 DMA 引擎的响应延迟取决于系统 AHB 总线负载与 uSDHC 的优先级。
引擎在突发传输期间不响应新请求，突发一结束即可服务。……传输以块为单位，
后续水位总按剩余字数设置（原文举例：31 字节块、突发 6 字，首突发后剩
(31-24)/4≈2 字，则下一突发为 2 字，再下一块恢复 6 字）。

#### 58.4.2.2 DMA 突发长度

【原文】Just like a CPU polling access, the DMA burst length for the internal DMA
engine can be from 1 to 16 words. The actual burst length for the DMA depends on
the lesser of the configured burst length or the remaining words of the current
block. … It is also acceptable to configure the burst length as the divisor of the
block size, so that each time the burst length will be the same.

【译文】与 CPU 轮询访问类似，内部 DMA 引擎的突发长度可为 1~16 字。实际突发
长度取"配置的突发长度"与"当前块剩余字数"中的较小者。……把突发长度配成
块长的因子亦可，这样每次突发长度都相同。

#### 58.4.2.3 AHB 主接口

【原文】It is possible that the internal AHB DMA engine could fail during the data
transfer. Upon detection of an AHB bus error during DMA transfer, the DMA engine
stops the transfer and goes to the idle state. At that point, the internal data
buffer stops receiving incoming data and sending out data. The DMAE bit in the
Interrupt Status register will be generated to host CPU to report a bus error
condition. Once the DMAE interrupt is received, the software shall send a CMD12 to
abort the current transfer and read the DS_ADDR bits of the DMA System Address
register to get the starting address of the corrupted block. After the DMA error is
fixed, the software should apply a data reset and re-start the transfer from this
address to recover the corrupted block.

【译文】内部 AHB DMA 引擎可能在传输中失败。检测到 AHB 总线错误时，引擎停止
传输进入空闲态；内部数据缓冲停止收发数据；中断状态寄存器的 DMAE 位产生、向
主机 CPU 报告总线错误。软件收到 DMAE 中断后应发 CMD12 中止当前传输，读 DMA
System Address 寄存器的 DS_ADDR 位取得损坏块的起始地址；错误排除后做一次
数据复位、从该地址重启传输以恢复损坏块。

#### 58.4.2.4 ADMA 引擎

【原文】In the SD Host Controller Standard, a new DMA transfer algorithm called the
ADMA (Advanced DMA) is defined. For Simple DMA, once the page boundary is reached,
a DMA interrupt will be generated and the new system address shall be programmed by
the Host Driver. The ADMA defines the programmable descriptor table in the system
memory. The Host Driver can calculate the system address at the page boundary and
program the descriptor table before executing ADMA. It reduces the frequency of
interrupts to the host system. Therefore, higher speed DMA transfers could be
realized since the Host MCU intervention would not be needed during long DMA based
data transfers. There are two types of ADMA: ADMA1 and ADMA2 in Host Controller.
ADMA1 can support data transfer of 4KB aligned data in system memory. ADMA2
improves the restriction so that data of any location and any size can be
transferred in system memory. Their formats of Descriptor Table are different.
ADMA can recognize all kinds of descriptors define in SD Host Controller Standard,
and if 'End' flag is detected in the descriptor, ADMA will stop after this
descriptor is processed.

【译文】SD 主机控制器标准定义了新的 DMA 传输算法——ADMA（高级 DMA）。简单
DMA 一旦到达页边界就会产生 DMA 中断、由主机驱动编程新系统地址；ADMA 则把
可编程的描述符表定义在系统内存中——主机驱动可在执行 ADMA 前算好页边界处
的系统地址并编好描述符表，从而减少对主机系统的中断频率；因此长时间 DMA
传输无需主机 MCU 干预，可实现更高速的 DMA 传输。ADMA 有两种：ADMA1 支持
系统内存中 4KB 对齐数据的传输；ADMA2 放宽了限制，系统内存中任意位置、
任意尺寸的数据均可传输，两者描述符表格式不同。ADMA 能识别标准定义的所有
描述符类型；检测到描述符带 'End' 标志时，处理完该描述符即停止。

### 58.4.3 带 IP 总线接口的寄存器堆

【原文】Register accesses via the IP Bus interface are actually on the Register
Bank.（图 58-12）

【译文】经 IP 总线接口的寄存器访问，实际落在寄存器堆上。

```
【图 58-12 复现：寄存器堆框图】
  IP Bus 信号 ──► 软件可见寄存器 ◄──► 写 1 清零功能阵列
                  │控制信号→其它模块 / 状态信号←其它模块
                  ▼
        Control/Status 与其它时钟域 ──► Synchronizer（同步器）
                  ▼
        Data Port ◄── Buffer Control
        （ips_xfr_err / ips_xfr_wait 亦接至 IP Bus）
```

#### 58.4.3.1 SD 协议单元

【原文】The SD protocol unit deals with all SD protocol affairs. The SD Protocol
Unit performs the following functions: • Acts as the bridge between the internal
buffer and the SD bus • Sends the command data as well as its argument serially •
Stores the serial response bit stream into corresponding registers • Detects the
bus state on the CMD/DAT lines • Monitors the interrupt from the SDIO card •
Asserts the read wait signal • Gates off the SD clock when buffer is announcing
danger status • Detects the write protect state. The SD Protocol Unit consists of
four sub modules: 1. SD control misc. 2. Command control. 3. Data control. 4.
Clock control.

【译文】SD 协议单元处理一切 SD 协议事务，执行以下功能：充当内部缓冲与 SD
总线之间的桥；串行发送命令及其参数；把串行响应比特流存入相应寄存器；检测
CMD/DAT 线上的总线状态；监视 SDIO 卡中断；发出 read wait 信号；缓冲告急时
门控关闭 SD 时钟；检测写保护状态。SD 协议单元含四个子模块：①SD 控制 misc；
②命令控制；③数据控制；④时钟控制。

#### 58.4.3.2 SD 控制 misc

【原文】In the SD control misc unit, the card detect (include the CD_B and DATA3
used as Card Detection), write protection and card interrupt are implemented. This
module monitors the signal level on all 8 data lines, the command lines, and
directly routes the level values into the Register Bank. The driver can use this
for debug purposes. The module also detects the WP (Write Protect) line. If WP is
active, writes to the register bank will be ignored. This module also drives the
LCTL output signal when the LCTL bit is set by the driver.

【译文】卡检测（含 CD_B 与 DATA3 兼作卡检测）、写保护与卡中断在 SD 控制
misc 单元实现。本模块监视全部 8 根数据线与命令线的信号电平，并把电平值直接
路由进寄存器堆——驱动可借此调试。本模块还检测 WP（写保护）线：WP 有效时，
对寄存器堆的写将被忽略。驱动置 LCTL 位时，本模块驱动 LCTL 输出信号。

#### 58.4.3.3 SD 时钟控制

【原文】If the internal data buffer is near full (for read) or near empty (for
write), the SD clock must be gated off to avoid buffer over/under-run, this module
will assert the gate of the output SD clock to shut the clock off. After the buffer
has space (for read) or has data (for write), the clock gate of this module will
open and the SD clock will be active again.

【译文】内部数据缓冲将满（读）或将空（写）时，必须门控关闭 SD 时钟以避免
上溢/下溢——本模块会关断输出 SD 时钟；待缓冲有空间（读）或有数据（写）
后，时钟门重新打开、SD 时钟恢复。

#### 58.4.3.4 命令控制

【原文】The Command Control module deals with the transactions on the CMD line.
… The CRC polynomials for the CMD are as follows: Generator polynomial:
G(x) = x7 + x3 + 1; M(x) = (first bit)·xⁿ + … + (last bit)·x⁰;
CRC[6:0] = Remainder [(M(x)·x⁷) / G(x)]（图 58-13：命令 CRC 移位寄存器）

【译文】命令控制模块处理 CMD 线上的事务。命令的 CRC 多项式：生成多项式
G(x) = x⁷ + x³ + 1；CRC[6:0] = [M(x)·x⁷ / G(x)] 的余数。（图 58-13 为命令
CRC 移位寄存器：CRC_IN 串入，经 CRC Bus[0..6] 移位链，从 CRC OUT 输出，
CLR_CRC/ZERO 控制清零。）

#### 58.4.3.5 数据控制

【原文】The Data Agent deals with the transactions on the eight data lines.
Moreover, this module also detects the busy state on the DATA0 line, and generates
the Read Wait state by the request from the Transceiver. The CRC polynomials for
the DATA are as follows: Generator polynomial: G(x) = x16 + x12 + x5 + 1; CRC[15:0]
= Remainder [(M(x)·x¹⁶) / G(x)]

【译文】数据代理处理八根数据线上的事务；本模块还检测 DATA0 线的 busy 状态，
并按收发器的请求产生 Read Wait 状态。数据的 CRC 多项式：生成多项式
G(x) = x¹⁶ + x¹² + x⁵ + 1；CRC[15:0] = [M(x)·x¹⁶ / G(x)] 的余数。

### 58.4.4 时钟与复位管理器

【原文】This module controls all the reset signals within the uSDHC. There are
four kinds of reset signals within uSDHC: 1. Hardware reset. 2. Software reset for
all logic. 3. Software reset for the data logic. 4. Software reset for the command
logic. All these signals are fed into this module and stable signals are generated
inside the module to reset all other modules. The module also gates off all the
inside signals.

【译文】本模块控制 uSDHC 内所有复位信号。uSDHC 内有四种复位信号：①硬件
复位；②全逻辑软件复位；③数据逻辑软件复位；④命令逻辑软件复位。所有这些
信号馈入本模块，在模块内生成稳定信号去复位其它模块；本模块还给所有内部
信号做门控。

### 58.4.5 时钟生成器

【原文】The Clock Generator generates the card CLK by peripheral source clock in
two stages. … The term "Base" represents the frequency of peripheral source clock.
The first divisor stage (controlled by uSDHCx_SYS_CTRL[DVS]) outputs an
intermediate clock (DIV), which can be Base, Base/2, Base/3, ..., or Base/16. The
second divisor stage (controlled by uSDHCx_SYS_CTRL[SDCLKFS]) outputs the actual
internal working clock (card_clk). This clock is the driving clock for all sub
modules of the SD Protocol Unit, and the sync FIFOs (see Figure 58-3) to
synchronize with the data rate from the internal data buffer. Please note, in SDR
mode and DDR mode, the CLK are different. • In SDR mode, CLK is equal to the
internal working clock (card_clk). • In DDR mode, CLK is equal to the card_clk/2.

【译文】时钟生成器用外设源时钟分两级产生卡 CLK。"Base"即外设源时钟频率。
第一级分频器（由 SYS_CTRL[DVS] 控制）输出中间时钟 DIV，可为 Base、Base/2、
Base/3 … Base/16。第二级分频器（由 SYS_CTRL[SDCLKFS] 控制）输出实际内部
工作时钟 card_clk——它是 SD 协议单元全部子模块与同步 FIFO（见图 58-3）的
驱动时钟，用于与内部数据缓冲的数据率同步。注意 SDR 与 DDR 模式下 CLK 不同：
SDR 模式 CLK = card_clk；DDR 模式 CLK = card_clk/2。

```
【图 58-14 复现：两级时钟分频器】
  Base ──► 一级分频 ÷(1..16) ─► DIV ──► 二级分频 ÷(1*,2,4,…,256) ─► card_clk
                                                                     │
                                                              SDR: CLK=card_clk
                                                              DDR: CLK=card_clk/2
```

> 译注：二级分频 "(1*)" 表示 SDCLKFS=0 时为 1 分频——即我们公式
> `base/((DVS+1)×2×SDCLKFS)` 中 SDCLKFS=0 的特例。

### 58.4.6 SDIO 卡中断

【原文】Information on Interrupts in 1-bit Mode, Interrupts in 4-bit Mode, and
Card Interrupt Handling are detailed in the sections below.

【译文】1 位模式中断、4 位模式中断与卡中断处理的信息见以下各节。

#### 58.4.6.1 1 位模式的中断

【原文】In this case the DATA1 pin is dedicated to providing the interrupt
function. An interrupt is asserted by pulling the DATA1 low from the SDIO card,
until the interrupt service is finished to clear the interrupt.

【译文】此模式下 DATA1 引脚专用于中断功能：SDIO 卡把 DATA1 拉低即声明中断，
直到中断服务完成清除该中断。

> 批次 3b 至此；58.4.6.2/.6.3（4 位模式中断与卡中断处理）并入批次 5c。

## 58.5 uSDHC 的初始化与应用（批次 5a：识别流程半章）

### 58.5.1 命令发送与响应接收的基本操作

【原文】Assuming the data type WORD is an unsigned 32-bit integer, the below flow
is a guideline for sending a command to the card(s):

【译文】假设数据类型 WORD 为无符号 32 位整数，下面的流程是向卡发送命令的指导：

```c
【send_command()/wait_for_response() 伪代码，逐行翻译】
send_command(cmd_index, cmd_arg, other requirements)
{
    WORD wCmd;                              // 拼装 Transfer Type 寄存器值的 32 位整数，
                                            // 建议以位域实现
    wCmd = (cmd_index & 0x3f) >> 24;        // 高 8 位 = '00'+命令号
    按 cmd_index 设置 CMDTYP、DPSEL、CICEN、CCCEN、RSTTYP、DTDSEL;
    if (使用内部 DMA) wCmd |= 0x1;
    if (多块传输) {
        置 MSBSEL;
        if (块数有限) {
            置 BCEN;
            if (要用 auto12 命令) 置 AC12EN;
        }
    }
    write_reg(CMDARG, cmd_arg);             // 配置命令参数
    write_reg(XFERTYP, wCmd);               // 写入即发出命令
}

wait_for_response(cmd_index)
{
    while (IRQ Status 的 CC 位未置位);      // 等命令完成
    读 IRQ Status，检查命令相关错误位;
    if (有错误位) 报错;
    写 1 清除 CC 位与所有命令错误位;
}
```

【原文】For the sake of simplicity, the function wait_for_response is implemented
here by means of polling. For an effective and formal way, the response is usually
checked after the Command Complete Interrupt is received. When doing this, make
sure the corresponding interrupt status bits are enabled. For some scenarios, the
response time-out is expected. For instance, after all cards respond to CMD3 and
go to the Standby State, no response to the Host when CMD2 is sent. The Host
Driver will deal with "fake" errors like this with caution.

【译文】为简洁起见，wait_for_response 在此用轮询实现。正式而高效的做法通常是
收到"命令完成中断"后再查响应，此时须确保相应中断状态位已使能。某些场景下
响应超时是预期行为——例如所有卡响应 CMD3 进入 Standby 态后再发 CMD2 无人
应答。主机驱动应谨慎处理这类"假错误"。

### 58.5.2 卡识别模式

【原文】When a card is inserted to the socket or the card was reset by the host,
the host needs to validate the operation voltage range, identify the cards,
request the cards to publish the Relative Card Address (RCA) or to set the RCA
for the MMC cards.

【译文】当卡插入卡座、或卡被主机复位后，主机需要验证工作电压范围、识别卡、
请求卡公布相对卡地址（RCA），或对 MMC 卡直接指定 RCA。

#### 58.5.2.1 卡检测

【原文】Here is the card detect sequence: • Set the CINSIEN bit to enable card
detection interrupt • When an interrupt from the uSDHC is received, check the
CINS bit in the Interrupt Status register to see if it was caused by card
insertion • Clear the CINSIEN bit to disable the card detection interrupt and
ignore all card insertion interrupts afterwards

【译文】卡检测序列：置 CINSIEN 位使能卡检测中断；收到 uSDHC 中断后，检查中断
状态寄存器的 CINS 位判断是否由插卡引起；清 CINSIEN 位关闭卡检测中断，此后
忽略所有插卡中断。（图 58-18：使能检测中断 → 等中断 → 查 CINS → 有卡则关
检测中断、进电压验证。）

#### 58.5.2.2 复位

【原文】The host consists of three types of resets: • Hardware reset (Card and
Host) which is driven by POR (Power On Reset) • Software reset (Host Only) is
initiated by the write operation on the RSTD, RSTC, or RSTA bits of the System
Control register to reset the data part, command part, or all parts of the Host
Controller, respectively • Card reset (Card Only). The command, "Go_Idle_State"
(CMD0), is the software reset command for all types of MMC cards, SD Memory
cards. This command sets each card into the Idle State regardless of the current
card state. For an SD I/O Card, CMD52 is used to write an I/O reset in the CCCR.
The cards are initialized with a default relative card address (RCA=0x0000) and
with a default driver stage register setting (lowest speed, highest driving
current capability).

【译文】复位共分三类：
- **硬件复位**（卡与主机）：由上电复位（POR）驱动。
- **软件复位**（仅主机）：通过写 System Control 寄存器的 RSTD、RSTC 或 RSTA 位
  发起，分别复位主机控制器的数据部分、命令部分或全部。
- **卡复位**（仅卡）：Go_Idle_State（CMD0）是对所有类型 MMC 卡、SD 存储卡的
  软件复位命令——无论卡当前处于什么状态，都把每张卡置入 Idle 态。对 SD I/O
  卡，用 CMD52 向 CCCR 写 I/O 复位。复位后卡以默认相对卡地址（RCA=0x0000）与
  默认驱动级设置（最低速度、最强驱动电流）初始化。

【图 58-19 + software_reset() 伪代码，逐行翻译】

```c
software_reset()
{
    set_bit(SYSCTRL, RSTA);    // 软件复位主机
    设置 DTOCV 与 SDCLKFS，得到约 400kHz 的 CLK;
    配置 IO 焊盘，把外部卡供电设到约 3.0V;
    轮询 PRSSTAT 的 CIHB 与 CDIHB，等两位都清零;
    set_bit(SYSCTRL, INTIA);   // 发 80 个时钟供卡上电
    send_command(CMD_GO_IDLE_STATE, <其它参数>);  // CMD0 复位卡
    或 send_command(CMD_IO_RW_DIRECT, <其它参数>);
}
```

> 译注：即 justiceBL `usdhc_hw_init()` + CMD0 的 RM 原版骨架。

#### 58.5.2.3 电压验证

【原文】All cards should be able to establish communication with the host using
any operation voltage in the maximum allowed voltage range specified in the card
specification. However, the supported minimum and maximum values for Vdd are
defined in the Operation Conditions Register (OCR) and may not cover the whole
range. Cards that store the CID and CSD data in the preload memory are only able
to communicate this information under data transfer Vdd conditions. This means if
the host and card have non-common Vdd ranges, the card will not be able to
complete the identification cycle, nor will it be able to send CSD data.
Therefore, a special command Send_Op_Cont (CMD1 for MMC), SD_Send_Op_Cont
(ACMD41 for SD Memory) and IO_Send_Op_Cont (CMD5 for SD I/O) is used. The voltage
validation procedure is designed to provide a mechanism to identify and reject
cards which do not match the Vdd range(s) desired by the host. This is
accomplished by the host sending the desired Vdd voltage window as the operand of
this command. Cards that can't perform the data transfer in the specified range
must discard themselves from further bus operations and go into the Inactive
State.

【译文】所有卡都应能在卡规范允许的最大电压范围内的任一工作电压下与主机建立
通信。但 Vdd 支持的最小/最大值定义在工作条件寄存器（OCR）中，未必覆盖整个
范围。把 CID/CSD 存在预载存储器里的卡，只能在数据传输级 Vdd 条件下交出这些
信息——若主机与卡的 Vdd 范围无交集，卡无法完成识别周期，也发不出 CSD。为此
使用专门命令：MMC 用 CMD1、SD 存储卡用 ACMD41、SD I/O 用 CMD5。电压验证机制
的目的是识别并拒绝与主机期望 Vdd 范围不匹配的卡：主机把期望的 Vdd 电压窗作为
命令操作数发出；无法在该范围内做数据传输的卡，必须把自己从后续总线操作中
除名、进入 Inactive 态。（原文另述：命令中省略电压范围即可先查询各卡、确定
公共范围，再除名超范围卡。）

【voltage_validation() 伪代码，主干翻译】

```c
voltage_validation(电压范围参数)
{
    把卡标记为 UNKNOWN;
    发 CMD5(IO_SEND_OP_COND, arg=0);          // 探测 SDIO
    if (CMD5 未超时) {                        // SDIO 命令被接受
        if (IO 功能数 > 0) {
            标记为 SDIO; IORDY = 0;
            while (IO OCR 响应的 IORDY 未置位) {  // 逐个 IO 功能设电压
                发 CMD5(带电压范围); 等响应;
            }
        }
        if (SDIO 卡内含存储部分) 标记为 SDCombo;
    }
    if (已是 SDIO) return;                    // 类型已定、电压已设
    发 CMD55(APP_CMD);                        // 应用命令前缀
    if (CMD55 无错) {                         // CMD55 被接受 → SD 家族
        发 ACMD41(带电压范围); 等响应;         // 电压范围已设
        if (类型仍 UNKNOWN) 标记为 SD;
        return;
    } else if (超时以外的错误) { 按程序特定方式处理; }
    else {                                    // CMD55 被拒 → 必是 MMC
        if (已标 SDCombo) { 改标 SDIO; 忽略或报错; return; }
        发 CMD1(SEND_OP_COND, 带电压范围);
        if (CMD1 超时) { 标记为 UNKNOWN; return; }
    }
}
```

#### 58.5.2.4 卡注册（Card Registry）

【原文】For the SD Card, the Identification process starts at a clock rate lower
than 400 kHz and the power voltage higher than 2.7 V (as defined by the Card
spec). At this time, the CMD line output drives are push-pull drivers instead of
open-drain. … The response to ACMD41 is the operation condition register of the
card. The same command shall be send to all of the new cards in the system.
Incompatible cards are put into the Inactive State. The host then issues the
command, All_Send_CID (CMD2), to each card to get its unique card identification
(CID) number. Cards that are currently unidentified (in the Ready State), send
their CID number as the response. After the CID is sent by the card, the card
goes into the Identification State. The host then issues Send_Relative_Addr
(CMD3), requesting the card to publish a new relative card address (RCA) that is
shorter than the CID. This RCA will be used to address the card for future data
transfer operations. Once the RCA is received, the card changes its state to the
Standby State. At this point, if the host wants the card to have an alternative
RCA number, it may ask the card to publish a new number by sending another
Send_Relative_Addr command to the card. The last published RCA is the actual RCA
of the card. The host repeats the identification process with CMD2 and CMD3 for
each card in the system until the last CMD2 gets no response from any of the
cards in system.

【译文】对 SD 卡：识别过程以低于 400kHz 的时钟、高于 2.7V 的供电开始（卡规范
定义）。此时 CMD 线输出驱动为推挽而非开漏。……ACMD41 的响应即卡的 OCR；同一
命令发给系统中所有新卡，不兼容卡进入 Inactive 态。随后主机发布 All_Send_CID
（CMD2）向每张卡索取唯一 CID；当前未识别的卡（Ready 态）以 CID 作响应，发完
后进入 Identification 态。接着主机发布 Send_Relative_Addr（CMD3），请求卡公布
一个比 CID 短的新 RCA——此后数据传输都以 RCA 寻址该卡。收到 RCA 后卡转入
Standby 态；主机此时若想换号，可再发一次 CMD3 让卡公布新号，**最后公布的
RCA 即实际 RCA**。主机对每张卡重复 CMD2/CMD3，直到最后一次 CMD2 无任何卡
响应。

【原文】For MMC operation, the host starts the card identification process in
open-drain mode … The open drain driver stages on the CMD line allow parallel
card operation during card identification. … The response to CMD1 is the "wired
OR" operation on the condition restrictions of all cards in the system. … All
unidentified cards (the cards in Ready State) simultaneously start sending their
CID numbers serially, while bit-wise monitoring their outgoing bit stream. Those
cards, whose outgoing CID bits do not match the corresponding bits on the command
line in any one of the bit periods, stop sending their CID immediately and must
wait for the next identification cycle. Since the CID is unique for each card,
only one card can be successfully send its full CID to the host. … Thereafter,
the host issues Set_Relative_Addr (CMD3) to assign to the card a relative card
address (RCA). Once the RCA is received the card state changes to the Stand-by
State, and the card does not react in further identification cycles, and its
output driver switches from open-drain to push-pull. The host repeats the
process, mainly CMD2 and CMD3, until the host receives a time-out condition to
recognize the completion of the identification process.

【译文】对 MMC 操作：主机以开漏模式开始识别（时钟同样 <400kHz、供电 >2.7V）。
CMD 线的开漏驱动级允许多卡在识别期间并行操作。……CMD1 的响应是系统内所有卡
条件限制的"线或"结果。……所有未识别卡（Ready 态）**同时**开始串行发送各自
CID，并逐位监视自己的发出比特流——任何一位与命令线上实际比特不符的卡立即
停止发送、等待下轮识别。因 CID 唯一，最终只有一张卡能发完整个 CID，该卡进入
Identification 态。此后主机发布 Set_Relative_Addr（CMD3）**为卡指定** RCA；
收到后卡转入 Standby 态，不再参与后续识别周期，输出驱动由开漏切为推挽。主机
重复此过程（主要是 CMD2 与 CMD3）直到超时，即识别完成。

【card_registry() 伪代码，主干翻译】

```c
card_registry()
{
    do {                                    // 为每张卡定 RCA，直到响应超时
        if (标记为 SDCombo 或 SDIO) {
            发 SET_RELATIVE_ADDR(0x00);      // 请 SDIO 卡公布 RCA
            从响应取 RCA;
        } else if (标记为 SD) {
            发 ALL_SEND_CID;
            if (响应超时) break;
            发 SET_RELATIVE_ADDR;            // SD 卡：卡自己公布 RCA
            从响应取 RCA;
        } else if (标记为 MMC) {
            发 ALL_SEND_CID;
            rca = 0x1;                       // 任意定 RCA，此处以 1 为例
            发 SET_RELATIVE_ADDR(0x1 << 16); // RCA 放高 16 位发出
        }
    } while (响应未超时);
}
```

> 译注：MMC 分支 `rca=0x1; CMD3(rca<<16)` 正是 justiceBL `emmc_init()` 里
> `card->rca = 0x00010000` 的原文出处；SD 分支"从响应取 RCA"对应 `sd_init()`
> 的 `card->rca = r & 0xFFFF0000`。

> 批次 5b（58.5.3 卡访问：块写/块读/挂起恢复/ADMA 用法/传输错误/卡中断）
> ——本批完成 58.5.3.1 与 58.5.3.2。

### 58.5.3 卡访问

【原文】Information about Block Write, Block Read, Suspense Resume, ADMA Usage,
Transfer Error, and Card Interrupt are detailed in the sections below.

【译文】块写、块读、挂起/恢复、ADMA 用法、传输错误与卡中断的信息见以下
各节。

#### 58.5.3.1 块写

【原文】Information on Normal Write, DDR Write, and Write with Pause are detailed
in the sections below.

【译文】普通写、DDR 写与暂停写的信息见以下各节。

##### 58.5.3.1.1 普通写（Normal Write）

【原文】During a block write (CMD24 - 27, CMD60, CMD61), one or more blocks of
data are transferred from the host to the card with a CRC appended to the end of each
block by the host. If the CRC fails, the card shall indicate the failure on the DATA
line. The transferred data will be discarded and not written, and all further
transmitted blocks (in multiple block write mode) will be ignored.

【译文】块写期间（CMD24~27、CMD60、CMD61），一个或多个数据块由主机传给
卡，主机在每个块末尾附上 CRC。若 CRC 失败，卡在 DATA 线上指示失败：已传
数据将被丢弃不写，多块写模式下后续所有块都被忽略。

【原文】If the host uses partial blocks whose accumulated length is not block
aligned and block misalignment is not allowed (CSD parameter WRITE_BLK_MISALIGN
is not set), the card detects the block misalignment error and aborts the programming
before the beginning of the first misaligned block. The card sets the ADDRESS_ERROR
error bit in the status register, and while ignoring all further data transfer, waits in
the Receive-data-State for a stop command. The write operation is also aborted if the
host tries to write over a write protected area.

【译文】若主机使用累计长度不按块对齐的部分块、且不允许块错位（CSD 参数
WRITE_BLK_MISALIGN 未置位），卡会检测出块错位错误并在第一个错位块开始前
中止编程：卡置状态寄存器的 ADDRESS_ERROR 位，忽略后续所有数据传输，停在
Receive-data 态等停止命令。主机试图写写保护区时，写操作同样被中止。

【原文】For MMC and SD cards, programming of the CID and CSD registers does not
require a previous block length setting. The transferred data is also CRC protected.
If a part of the CSD or CID register is stored in ROM, then this unchangeable part
must match the corresponding part of the receive buffer. If this match fails, then the
card will report an error and not change any register contents.

【译文】对 MMC 与 SD 卡，烧写 CID/CSD 寄存器不需要预先设块长；传输的数据
同样受 CRC 保护。若 CSD/CID 的一部分存在 ROM 里，这个不可变部分必须与接收
缓冲的对应部分一致——不匹配则卡报错、不改变任何寄存器内容。

【原文】For all types of cards, some may require long and unpredictable periods of
time to write a block of data. After receiving a block of data and completing the CRC
check, the card will begin writing and hold the DATA line low if its write buffer is
full and unable to accept new data from a new WRITE_BLOCK command. The host may
poll the status of the card with a SEND_STATUS command (CMD13) or other means for
SDIO cards at any time, and the card will respond with its status. The responded
status indicates whether the card can accept new data or whether the write process is
still in progress. The host may deselect the card by issuing a CMD7 (to select a
different card) to place the card into the Standby State and release the DATA line
without interrupting the write operation. When re-selecting the card, it will reactivate
the busy indication by pulling DATA to low if the programming is still in progress and
the write buffer is unavailable.

【译文】对任何类型的卡，写一块数据都可能需要漫长且不可预测的时间。卡收完
一块数据并完成 CRC 校验后开始写入；若其写缓冲已满、无法再接受新
WRITE_BLOCK 命令的数据，会把 DATA 线拉低（busy）。主机可随时用
SEND_STATUS（CMD13）轮询卡状态（SDIO 卡用其它手段），卡回以状态——表明
能否接受新数据、或写入是否仍在进行。主机可以发 CMD7 改选别的卡，把这张卡
放回 Standby 态、释放 DATA 线，而不打断写操作；重新选中它时，若编程仍在
进行且写缓冲不可用，卡会重新拉低 DATA 线激活 busy 指示。

【原文】The software flow to write to a card incorporates the internal DMA and the
write operation is a multi-block write with the Auto CMD12 enabled. For the other two
methods (by means of external DMA or CPU polling status) with different transfer
methods, the internal DMA parts should be removed and the alternative steps should be
straightforward.

【译文】下面写卡的软件流程以内部 DMA 编写，操作为使能 Auto CMD12 的多块
写。另两种方法（外部 DMA 或 CPU 轮询状态）只需去掉内部 DMA 相关步骤、代
之以相应步骤即可。

【写卡软件流程，逐步翻译】

1. 检查卡状态，等卡准备好数据。
2. 设置卡块长/块尺寸：SD/MMC 卡用 SET_BLOCKLEN（CMD16）；SDIO 卡或
   SDCombo 卡的 I/O 部分用 IO_RW_DIRECT（CMD52）设 CCCR（功能 0）或 FBR
   （功能 1~7）里的 I/O Block Size 位域。
3. 把 uSDHC 块长寄存器设成与第 2 步相同的块长。
4. 设 uSDHC 块数寄存器（NOB），例如 5。
5. 关 buffer write ready 中断；发带数据的命令时配置 DMA 设置并使能 uSDHC
   DMA；同时置 AC12EN 位。
6. 等 Transfer Complete 中断。
7. 检查状态位：写 CRC 错误、或 auto12 命令收发期间发生的其它错误。

##### 58.5.3.1.2 DDR 写

【原文】uSDHC supports dual data rate mode.（流程与普通写同理，逐步翻译）

【译文】uSDHC 支持双倍数据速率模式。DDR 模式写卡流程：

1. 检查卡状态，等卡准备好数据。
2. 对 eMMC4.4 卡，块长只能设 512 字节。
3. 设块数寄存器（NOB），例如 5。
4. 用 SWITCH（CMD6）把 eMMC4.4 卡设为高速模式。
5. 用 SWITCH（CMD6）把 eMMC4.4 卡总线设为 4/8 位 DDR 模式。
6. 关 buffer write ready 中断；配置 DMA 并使能 uSDHC DMA；**置 DDR_EN 位**；
   置 AC12EN。
7. 等 Transfer Complete 中断。
8. 检查写 CRC 错误或 auto12 期间的其它错误。

##### 58.5.3.1.3 暂停写（Write with Pause）

【原文】The write operation can be paused during the transfer. Instead of stopping
the CLK at any time to pause all the operations, which is also inaccessible to the
Host Driver, the Driver can set the Stop At Block Gap Request (SABGREQ) bit in the
Protocol Control register to pause the transfer between the data blocks. As there is
no time-out condition in a write operation during the data blocks, a write to all
types of cards can be paused in this way, and if the DATA0 line is not required to
de-assert to release the busy state, no suspend command is needed.

【译文】写操作可在传输中暂停。与其在任何时刻停 CLK 来暂停一切（主机驱动
也做不到），驱动可以置协议控制寄存器的"块间隙停止请求"（SABGREQ）位，把
传输暂停在两个数据块之间。由于写操作的数据块阶段没有超时条件，所有类型卡
的写都能这样暂停；且若不需要 DATA0 线释放 busy 态，就无需挂起命令。

【暂停写流程，逐步翻译】（1~5 步同普通写）

6. 置 SABGREQ 位。
7. 等 Transfer Complete 中断。
8. 清 SABGREQ 位。
9. 检查写 CRC 错误。
10. 置 CREQ 位继续写操作。
11. 等 Transfer Complete 中断。
12. 检查写 CRC 错误或 auto12 期间其它错误。

【原文】The number of blocks left during the data transfer is accessible by reading
the contents of the BLKCNT field in the Block Attribute register. As the data
transfer and the setting of the SABGREQ bit are concurrent, and the delay of
register read and the register setting, the actual number of blocks left may not be
exactly the value read earlier. The Driver shall read the value of BLKCNT after the
transfer is paused and the Transfer Complete interrupt is received. It is also
possible the last block has begun when the Stop At Block Gap Request is sent to the
buffer. In this case, the next block gap is actually the end of the transfer. These
types of requests are ignored and the Driver should treat this as a non-pause
transfer and deal with it as a common write operation.

【译文】传输中剩余块数可读块属性寄存器的 BLKCNT 字段获得。由于数据传输与
SABGREQ 置位是并发的、寄存器读写又有延迟，实际剩余块数未必等于先前读到的
值——驱动应在传输暂停、收到 Transfer Complete 中断后再读 BLKCNT。还有一种
可能：SABGREQ 送到缓冲时最后一块已经开始，此时下一个"块间隙"实际就是传输
终点——这类请求会被忽略，驱动应按普通（无暂停）写处理。

【原文】When the write operation is paused, the data transfer inside the Host
System is not stopped, and the transfer is active until the data buffer is full.
Because of this (if not needed), it is recommended to avoid using the Suspend
Command for the SDIO card. This is because when such a command is sent, the uSDHC
thinks the System will switch to another function on the SDIO card, and flush the
data buffer. The uSDHC takes the Resume Command as a normal command with data
transfer, and it is left for the Driver to set all the relevant registers before the
transfer is resumed. If there is only one block to send when the transfer is
resumed, the MSBSEL and BCEN bits of the Transfer Type register are set as well as
the AC12EN bit. However, the uSDHC will automatically send a CMD12 to mark the end
of the multi-block transfer.

【译文】写暂停期间主机系统内部的数据搬运并不停止，会一直进行到缓冲满。因此
（如无必要）建议对 SDIO 卡避免使用挂起命令——该命令一发，uSDHC 认为系统
要切换 SDIO 卡上的其它功能，会冲刷数据缓冲。uSDHC 把恢复命令当作带数据的
普通命令，恢复传输前的一切相关寄存器由驱动负责设置。若恢复时只剩一块要发，
Transfer Type 寄存器的 MSBSEL、BCEN 与 AC12EN 位照设，uSDHC 仍会自动发
CMD12 标记多块传输结束。

#### 58.5.3.2 块读

【原文】Information about Normal Read, DDR Read, Read with Pause, and DLL (Delay
Line) in Read Path are detailed in the sections below.

【译文】普通读、DDR 读、暂停读与读通路中的 DLL（延迟线）见以下各节。

##### 58.5.3.2.1 普通读（Normal Read）

【原文】For block reads, the basic unit of data transfer is a block whose maximum
size is stored in areas defined by the corresponding card specification. A CRC is
appended to the end of each block, ensuring data transfer integrity. The CMD17,
CMD18, CMD53, CMD60, CMD61, and so on, can initiate a block read. After completing
the transfer, the card returns to the Transfer State. For multi blocks read, data
blocks will be continuously transferred until a stop command is issued.

【译文】块读的基本传输单位是块，最大尺寸存于相应卡规范定义的区域；每块末尾
附 CRC 保证传输完整性。CMD17、CMD18、CMD53、CMD60、CMD61 等可发起块读。
传输完成后卡回到 Transfer 态；多块读时数据块持续传输，直到发出停止命令。

【读卡软件流程，逐步翻译】（原文以内部 DMA + Auto CMD12 多块读为例）

1. 检查卡状态，等卡准备好数据。
2. 设置卡块长/块尺寸（SD/MMC 用 CMD16；SDIO 用 CMD52 设 CCCR/FBR 的 I/O
   Block Size）。
3. uSDHC 块长寄存器设成与第 2 步相同。
4. 设块数寄存器（NOB），例如 5。
5. 关 buffer read ready 中断；配置 DMA 并使能 uSDHC DMA；置 AC12EN。
6. 等 Transfer Complete 中断。
7. 检查读 CRC 错误或 auto12 期间其它错误。

> 译注：我们的 `mmc_read_blocks()` 是此流程的 PIO 单块版——第 5 步换成
> "开 BRR 状态位 + DPSEL 命令"，第 6 步换成"等 BRR → 舀 128 字 → 等 TC"。

##### 58.5.3.2.2 DDR 读

【原文】uSDHC supports dual data rate mode.（流程同 DDR 写骨架）

【译文】DDR 读流程（与 DDR 写同构）：检查卡状态 → eMMC4.4 块长固定 512 →
设 NOB → CMD6 切高速 → CMD6 切 4/8 位 DDR → 配 DMA、**置 DDR_EN**、置
AC12EN → 等 Transfer Complete → 查读 CRC/auto12 错误。

##### 58.5.3.2.3 暂停读（Read with Pause）

【原文】The read operation is not generally able to pause. Only the SDIO card (and
SDCombo card working under I/O mode) supporting the Read Wait feature can pause
during the read operation. If the SDIO card supports Read Wait (SRW bit in CCCR
register is 1), the Driver can set the SABGREQ bit in the Protocol Control register
to pause the transfer between the data blocks. Before setting the SABGREQ bit, make
sure the RWCTL bit in the Protocol Control register is set, otherwise the uSDHC
will not assert the Read Wait signal during the block gap and data corruption
occurs. It is recommended to set the RWCTL bit once the Read Wait capability of the
SDIO card is recognized.

【译文】读操作一般不能暂停。只有支持 Read Wait 特性的 SDIO 卡（及工作在
I/O 模式下的 SDCombo 卡）才能在读操作中暂停：若 SDIO 卡支持 Read Wait
（CCCR 寄存器的 SRW 位为 1），驱动可置协议控制寄存器的 SABGREQ 位，把传输
暂停在数据块之间。**置 SABGREQ 前必须确保 RWCTL 位已置位**，否则 uSDHC
不会在块间隙发出 Read Wait 信号、数据将损坏。建议一旦识别出 SDIO 卡具备
Read Wait 能力就置好 RWCTL 位。

【暂停读流程，逐步翻译】（1~4 步同普通读）

5. 设 uSDHC 块长寄存器与卡的块长一致。
6. 设块数寄存器（NOB），例如 5。
7. 关 buffer read ready 中断、配置 DMA、使能 DMA 时置 AC12EN。
8. 置 SABGREQ 位。
9. 等 Transfer Complete 中断。
10. 清 SABGREQ 位。
11. 检查读 CRC 错误。
12. 置 CREQ 位继续读操作。
13. 等 Transfer Complete 中断。
14. 检查读 CRC 错误、或 auto12 命令收发期间的其它错误。

【原文】Like the write operation, it is possible to meet the ending block of the
transfer when paused. In this case, the uSDHC will ignore the Stop At Block Gap
Request and treat it as a command read operation. Unlike the write operation, there
is no remaining data inside the buffer when the transfer is paused. All data
received before the pause will be transferred to the Host System. No matter if the
Suspend Command is sent or not, the internal data buffer is not flushed. If the
Suspend Command is sent and the transfer is later resumed by means of a Resume
Command, the uSDHC takes the command as a normal one accompanied with data
transfer. It is left for the Driver to set all the relevant registers before the
transfer is resumed. If there is only one block to send when the transfer is
resumed, the MSBSEL and BCEN bits of the Transfer Type register are set, as well as
the AC12EN bit. However, the uSDHC will automatically send the CMD12 to mark the
end of multi-block transfer.

【译文】与写操作一样，暂停时也可能恰好遇到传输的最后一块——此时 uSDHC 会
忽略"块间隙停止请求"，当作一次普通读处理。与写操作不同：读暂停时缓冲内没
有残留数据，暂停前收到的所有数据都已交给主机系统；无论是否发挂起命令，
内部数据缓冲都不会被冲刷。若发过挂起命令、之后又用恢复命令恢复传输，
uSDHC 把恢复命令当作带数据的普通命令，恢复前的一切相关寄存器由驱动设置；
恢复时只剩一块的话，MSBSEL、BCEN 与 AC12EN 照设，uSDHC 仍自动发 CMD12
标记多块传输结束。



##### 58.5.3.2.4 读通路中的 DLL（延迟线）

【原文】The DLL (Delay Line) is newly added to assist in sampling read data. The DLL
provides the ability to programmatically select a quantized delay (in fractions of the
clock period) regardless of on-chip variations such as process, voltage and
temperature (PVT). The reasons why the DLL is needed for uSDHC are 1.) the path of
read data traveling from card to host varies. 2.) in SD/MMC DDR mode the minimum
input setup and hold time are both at 2.5 ns. The data sampling window is so small
that the delay of loopback clock needs to be accurate and consistent regardless of
PVT. The DLL takes the divided card_clk as the reference clock and loopback clock as
the input clock. It then generates a delayed version of the input clock according to
the programmed target delay.

【译文】DLL（延迟线）为辅助采样读数据而新增。DLL 提供可编程选择量化延迟
（以时钟周期分数为单位）的能力，且不受工艺/电压/温度（PVT）芯片内差异影响。
uSDHC 需要 DLL 的原因：①卡到主机的读数据路径存在差异；②SD/MMC DDR 模式
下最小输入建立与保持时间均为 2.5ns——数据采样窗口极小，回路时钟的延迟必须
无视 PVT 地精确且一致。DLL 以分频后的 card_clk 为参考时钟、回路时钟为输入
时钟，按编程的目标延迟生成输入时钟的延迟版本。

【原文】The DLL can be disabled or bypassed, and it can also be manually set for a
fixed delay in override mode. … Another DLL mode is target value mode. In this
mode, the DLL will automatically adjust the number of delay cells according to
target value set by user and PVT changes. Be aware that target value is in units of
1/32 of the clock reference period. If the card_clk is 100MHz, then the reference
clock period is 10ns, setting target value of 16 means 5ns = (16/32)×10ns. Software
can disable automatic update by setting dll_gate_update bit.（图 58-20）

【译文】DLL 可禁用或旁路，也可在 override 模式下手动设固定延迟（override 值
即延迟单元数，此模式无需 DLL_enable）。另一模式为目标值模式：DLL 按用户设
定的目标值与 PVT 变化自动调整延迟单元数。注意目标值单位为参考时钟周期的
1/32——如 card_clk=100MHz（周期 10ns），目标值 16 即 (16/32)×10ns = 5ns。
软件可用 dll_gate_update 位禁止自动更新。（图 58-20：读数据从卡经延迟线/异步
缓冲入 uSDHC，DDR_EN 时 card_clk 二分频，DLL 参考为 ipp_card_clk_in_dll。）

【原文】Since the user may change the frequency of the card_clk from time to time by
changing SDCLKFS[7:0]/DVS[3:0], the software must adjust the delay value … The
following is the correct flow, which should be ignored if DLL_CTRL_ENABLE is not
set. Step 1: Set DLL_CTRL_RESET bit. Step 2: Configure the SDCLKFS[7:0] and
DVS[3:0]. Step 3: Wait until SDSTB is asserted. Step 4: clear DLL_CTRL_RESET bit.
Step 5: Wait until both DLL_STS_SLV_LOCK and DLL_STS_REF_LOCK are asserted.
Step 6: set DLL_CTRL_SLV_FORCE_UPD. Step 7: clear DLL_CTRL_SLV_FORCE_UPD.
NOTE: Software should make sure the DLL_CTRL_SLV_FORCE_UPD is lasted for at
least one card_clk. So software may need to add some delay between step6 and step7.

【译文】用户随时可能改 SDCLKFS/DVS 换 card_clk 频率，软件必须相应调整延迟
值。正确流程如下（未置 DLL_CTRL_ENABLE 时可忽略）：
①置 DLL_CTRL_RESET；②配置 SDCLKFS/DVS；③等 SDSTB 置位；④清
DLL_CTRL_RESET；⑤等 DLL_STS_SLV_LOCK 与 DLL_STS_REF_LOCK 均置位；
⑥置 DLL_CTRL_SLV_FORCE_UPD；⑦清之。**注意**：步骤 ⑥ 必须保持至少一个
card_clk——⑥⑦之间软件可能需要加延时。

#### 58.5.3.3 挂起/恢复（Suspend Resume）

【原文】The uSDHC supports the Suspend Resume operations of SDIO cards, although
slightly differently than the suggested implementation of Suspend in the SDIO card
specification.

【译文】uSDHC 支持 SDIO 卡的挂起/恢复操作，但与 SDIO 卡规范建议的实现略有
不同。

##### 58.5.3.3.1 挂起（Suspend）

【原文】After setting the SABGREQ bit, the Host Driver may send a Suspend command
to switch to another function of the SDIO card. The uSDHC does not monitor the
content of the response, so it doesn't know if the Suspend command succeeded or not.
Accordingly, it doesn't de-assert Read Wait for read pause. To solve this problem,
the Driver shall not mark the Suspend command as a "Suspend" (i.e. setting the
CMDTYP bits to 01). Instead, the Driver shall send this command as if it were a
normal command, and only when the command succeeds, and the BS bit is set in the
response, can the Driver send another command marked as "Suspend" to inform the
uSDHC that the current transfer is suspended.

【译文】置 SABGREQ 后，主机驱动可发挂起命令去切换 SDIO 卡的另一个功能。
uSDHC 不监视响应内容，所以不知道挂起命令是否成功，也就不会为读暂停撤销
Read Wait。解决办法：驱动**不要**把挂起命令标记为"Suspend"（即 CMDTYP 置
01），而是当普通命令发；只有命令成功、响应中 BS 位置位后，才再发一条标记
为"Suspend"的命令告知 uSDHC 当前传输已挂起。

【挂起操作序列，逐步翻译】

1. 置 SABREQ 位，在块间隙暂停当前数据传输。
2. BGE 位置位后，发挂起命令挂起活动功能——**CMDTYP 必须为 2'b00**（普通）。
3. 检查响应中 CCCR 的 BS 位：为 1 则重复本步，直到 BS 清零、或按驱动策略
   放弃挂起。
4. 向被挂起功能再发一条普通 I/O 命令，其 **CMDTYP 必须为 2'b01**——uSDHC
   检测到这一特殊设置即知传输已挂起（并保存上下文寄存器——供恢复用）。

##### 58.5.3.3.2 恢复（Resume）

【原文】To resume the data transfer, a Resume command shall be issued: 1. To resume
the suspended function, restore the context register with the saved value in step #5
of the Suspend operation above. 2. Send the Resume command. In the Transfer Type
register, all bit fields are set to the value as if this were another ordinary data
transfer, instead of a transfer resume (except the CMDTYP is set to 2'b10). 3. If
the Resume command has responded, the data transfer will be resumed.

【译文】发恢复命令以恢复数据传输：
1. 恢复被挂起的功能：用挂起操作第 5 步保存的值还原上下文寄存器。
2. 发恢复命令。Transfer Type 寄存器的所有位域按"另一次普通数据传输"设置，
   而非传输恢复——**唯一例外是 CMDTYP 置 2'b10**。
3. 恢复命令得到响应后，数据传输即恢复。

#### 58.5.3.4 ADMA 用法

【原文】To use the ADMA in a data transfer, the Host Driver must prepare the correct
descriptor chain prior to sending the read/write command. The steps to prepare the
correct descriptor chain are: …（步骤 1~7）

【译文】数据传输使用 ADMA 时，主机驱动必须在发读/写命令前备好正确的描述符
链。准备步骤：
1. 建一个描述符设定本组描述符要传的数据长度——长度须为块长的偶数倍。
2. 再建描述符传输该描述符所设地址的数据——**数据地址必须在页边界（4KB
   对齐）**。
3. 必要时建一个 Link 描述符，含下一描述符的地址。（步骤 1~3 构成一组。）
4. 重复 1~3 直到建完所有描述符。
5. 最后一个描述符置 End 标志为 1，并确保全部描述符的总长 = 块属性寄存器
   配置的 块长×块数。
6. 把 ADMA System Address 寄存器设为第一个描述符的地址；把协议控制寄存器
   的 DMAS 字段设为 01 选择 ADMA。
7. 发读/写命令，Transfer Type 寄存器 DMAEN=1。

【原文】Steps 1 ~ 5 are independent of step 6, so step 6 can finish before steps 1 ~
5. Regarding the descriptor configuration, it is recommended not to use the Link
descriptor as it requires extra system memory access.

【译文】步骤 1~5 与步骤 6 相互独立，6 可先于 1~5 完成。描述符配置方面，**
建议不用 Link 描述符**——它需要额外的系统内存访问。

#### 58.5.3.5 传输错误

【原文】Information about CRC, Internal DMA, Transfer ADMA, and Auto CMD12
Errors are detailed in the sections below.

【译文】CRC、内部 DMA、传输 ADMA 与 Auto CMD12 错误的信息见以下各节。

##### 58.5.3.5.1 CRC 错误

【原文】It is possible at the end of a block transfer, that a write CRC status error
or read CRC error occurs. For this type of error the latest block received shall be
discarded. This is because the integrity of the data block is not guaranteed. It is
recommended to discard the following data blocks and re-transfer the block from the
corrupted one. For a multi-block transfer, the Host Driver shall issue a CMD12 to
abort the current process and start the transfer by a new data command. In this
scenario, even when the AC12EN and BCEND bits are set, the uSDHC does not
automatically send a CMD12 because the last block is not transferred. On the other
hand, if it is within the last block that the CRC error occurs, an Auto CMD12 will
be sent by the uSDHC. In this case, the Driver shall re-send or re-obtain the last
block with a single block transfer.

【译文】块传输结束时可能发生写 CRC 状态错误或读 CRC 错误。此类错误应丢弃
最近收到的块——其数据完整性无保证；建议连同后续块一起丢弃、从损坏块起重传。
多块传输时主机驱动应发 CMD12 中止当前过程、用新数据命令重启传输——此场景
下即使 AC12EN 与 BCEND 已置位，uSDHC 也不会自动发 CMD12（因为最后一块尚未
传完）。反之，若 CRC 错误发生在最后一块内，uSDHC 会发出 Auto CMD12——此时
驱动应改用单块传输重发/重收最后一块。

##### 58.5.3.5.2 内部 DMA 错误

【原文】During the data transfer with internal Simple DMA, if the DMA engine
encounters some error on the AHB bus, the DMA operation is aborted and DMA Error
interrupt is sent to the Host System. When acknowledged by such an interrupt, the
Driver shall calculate the start address of data block in which the error occurs.
The start address can be calculated by either: 1. Reading the DMA System Address
register. … 2. Reading the BLKCNT field of the Block Attribute register. …（When
the BCEN bit is not set, the contents of the Block Attribute register does not
change, so this method does not work.）When a DMA error occurs, it is recommended
to abort the current transfer by means of a CMD12 (for multi block transfer), apply
a reset for data, and re-start the transfer from the corrupted block to recover
from the error.

【译文】内部简单 DMA 传输期间，DMA 引擎在 AHB 总线上遇到错误时：DMA 操作
中止，向主机系统发 DMA 错误中断。驱动收到中断后应算出出错数据块的起始地址，
算法有二：①读 DMA System Address 寄存器——错误发生在上一个突发，结合块长、
上一突发长度与下一突发的起始地址即可得损坏块起始地址；②读块属性寄存器的
BLKCNT 字段——由剩余块数、总块数、传输起始地址与块尺寸推算（**BCEN 未置位
时块属性寄存器内容不变，此法失效**）。DMA 错误的处理建议：发 CMD12 中止
（多块时）、做数据复位、从损坏块起重启传输以恢复。

##### 58.5.3.5.3 传输 ADMA 错误

【原文】There are 3 kinds of possible ADMA errors. The AHB transfer, invalid
descriptor, and data-length mismatch errors. Whenever these errors occur, the DMA
transfer stops and the corresponding error status bit is set. For acknowledging the
status, the Host Driver should recover the error as shown below and re-transfer
from the place of interruption. 1. AHB transfer error: … 2. Invalid descriptor
error: For such errors, it is recommended to retrieve the transfer context, reset
for the data part and re-create the descriptor chain from the invalid descriptor
and issue a new transfer. As the data to transfer now may be less than the previous
setting, the data length configured in the new descriptor chain should match the
new value. 3. Data-length mismatch error: It is similar to recover from this
error. …

【译文】ADMA 可能的错误有三种：AHB 传输错误、无效描述符、数据长度不匹配。
任一发生时 DMA 传输停止、相应错误状态位置位。驱动按下列方式恢复并从中断
处重传：
1. **AHB 传输错误**：可发生在数据传输或描述符取指期间——两种情况都建议
   取回传输上下文、做数据部分复位、重传损坏块（无损坏块则传下一块）。
2. **无效描述符错误**：取回上下文、数据部分复位、从无效描述符起重建描述
   符链并发起新传输；此时待传数据可能少于原设置，新链中配置的数据长度必须
   匹配新值。
3. **数据长度不匹配错误**：恢复方法类似——轮询相关寄存器取回上下文、数据
   部分复位、配置新描述符链，还有数据就再传一次；数据长度同样必须匹配。

##### 58.5.3.5.4 Auto CMD12 错误

【原文】After the last block of the multi block transfer is sent or received, and
the AC12EN bit is set when the data transfer is initiated by the data command, the
uSDHC automatically sends a CMD12 to the card to stop the transfer. When errors
with this command occur, it is recommended to the Driver to deal with the
situations in the following manner:（后续逐条处理建议见原文次页——批次 5c 补）

【译文】多块传输最后一块收发完、且发起数据命令时 AC12EN 已置位，uSDHC 自动
向卡发 CMD12 停止传输。该命令出错时驱动应按下列方式处理（逐条清单如下）。

##### 58.5.3.5.4 续：Auto CMD12 错误处理清单

【原文】When errors with this command occur, it is recommended to the Driver to
deal with the situations in the following manner: 1. Auto CMD12 response
time-out. It is not certain whether the command is accepted by the card or not.
The Driver should clear the Auto CMD12 error status bits and resend the CMD12
until it is accepted by the card. 2. Auto CMD12 response CRC error. Since card
responds to the CMD12, the card will abort the transfer. The Driver may ignore
the error and clear the error status bit. 3. Auto CMD12 conflict error or not
sent. The command is not sent, so the Driver shall send a CMD12 manually.

【译文】该命令出错时，建议驱动按下列方式处理：
1. **Auto CMD12 响应超时**：不确定卡是否接受了命令。驱动应清除 Auto CMD12
   错误状态位，**重发 CMD12 直到卡接受**。
2. **Auto CMD12 响应 CRC 错误**：卡既然响应了 CMD12，卡就会中止传输。驱动
   **可以忽略此错误**、清错误状态位即可。
3. **Auto CMD12 冲突错误或未发出**：命令根本没发出去——驱动**手动补发一条
   CMD12**。

#### 58.5.3.6 卡中断

【原文】The external cards can inform the Host Controller by means of some special
signals. For the SDIO card, it can be the low level on the DATA1 line during some
special period. The uSDHC only monitors the DATA1 line and supports the SDIO
interrupt. When the SDIO interrupt is captured by the uSDHC, and the Host System is
informed by the uSDHC asserting the uSDHC interrupt line, the interrupt service
from the Host Driver is called. As the interrupt source is controlled by the
external card, the interrupt from the SDIO card must be serviced before the CINT
bit is cleared by written.

【译文】外部卡可通过某些特殊信号通知主机控制器；对 SDIO 卡，是特定期间
DATA1 线上的低电平。uSDHC 只监视 DATA1 线、只支持 SDIO 中断。uSDHC 捕获
SDIO 中断后拉起 uSDHC 中断线通知主机系统，主机驱动的中断服务被调用。由于
中断源由外部卡控制，**SDIO 卡的中断必须在写 1 清除 CINT 位之前被服务完**。

### 58.5.4 切换功能（Switch Function）

【原文】A switch command must be issued by the Host Driver to enable new features
added to the SD/MMC spec. SD/MMC cards can transfer data at bus widths other
than 1-bit. Different speed mode are also defined. To enable these features, a
switch command shall be issued by the Host Driver. For SDIO cards, the high
speed mode/DDR50/SDR50/SDR104 are enabled by writing the EHS bit in the CCCR
register after the SHS bit is confirmed. For SD cards, the high speed
mode/DDR50/SDR50/SDR104 are queried and enabled by a CMD6 (with the mnemonic
symbol as SWITCH_FUNC). For MMC cards, the high speed mode/HS200 are queried by
a CMD8 and enabled by a CMD6 (with the mnemonic symbol as SWITCH). The SDR
4-bit, SDR 8-bit, DDR 4-bit and DDR 8-bit width of the MMC is also enabled by
the SWITCH command, but with a different argument. These new functions can also
be disabled by a software reset. For SDIO cards it can be done by setting the
RES bit in the CCCR register. For other cards, it can be accomplished by
issuing a CMD0. This method of restoring to the normal mode is not recommended
because a complete identification process is needed before the card is ready
for data transfer. For the sake of simplicity, the following pseudocode
examples do not show current capability check, which is recommended in the
function switch process.

【译文】SD/MMC 规范新增的功能须由主机驱动发切换命令来启用。SD/MMC 卡能以
1 位以外的总线宽度传输数据，还定义了不同的速度模式。SDIO 卡的高速/
DDR50/SDR50/SDR104 模式：确认 SHS 位后写 CCCR 的 EHS 位使能。SD 卡：用
CMD6（SWITCH_FUNC）查询并使能。MMC 卡：用 CMD8 查询、CMD6（SWITCH）使能
高速/HS200；MMC 的 SDR 4/8 位、DDR 4/8 位也用 SWITCH 命令使能，只是参数
不同。这些新功能也能被软件复位禁用：SDIO 卡置 CCCR 的 RES 位；其它卡发
CMD0——但不推荐这种恢复方式，因为之后要重走完整识别流程卡才能再传数据。
以下伪代码为简洁起见未包含"当前能力检查"（切换流程中推荐先做）。

#### 58.5.4.1 查询/使能/禁用 SDIO 高速模式

```c
【enable_sdio_high_speed_mode 逐行译】
    发 CMD52 查询地址 0x13 的 SHS 位;
    if (SHS==0) 报"SDIO 卡不支持高速模式"并返回;
    发 CMD52 置地址 0x13 的 EHS 位，写后读确认 EHS 已置位;
    改分频值/系统时钟，产生约 50MHz 的 card_clk;
    （此后数据事务与普通模式相同）

【disable_sdio_high_speed_mode 逐行译】
    发 CMD52 清 EHS 位，写后读确认已清;
    改分频，产生 25MHz 以下的 card_clk;
```

#### 58.5.4.2 查询/使能/禁用 SD 高速/SDR50/SDR104/DDR50

```c
【enable_sd_speed_mode 逐行译】
    BLKCNT=1 块，BLKSIZE=64 字节;
    发 CMD6(arg=0xFFFFFx) 并读 R1 响应附带的 64 字节数据;
        // 高速 x=1; SDR50 x=2; SDR104 x=3; DDR50 x=4
    等数据传输完成位置位;
    检查收到的 512 位中的功能支持位;
    if (位401==0) 报"不支持高速模式"返回;
    if (位402==0) 报"不支持 SDR50"返回;
    if (位403==0) 报"不支持 SDR104"返回;
    if (位404==0) 报"不支持 DDR50"返回;
    发 CMD6(arg=0x80FFFFFx) 再读 64 字节;   // 0x80 前缀=执行切换
    检查位域 379~376 是否为 0xF;
    if (为 0xF) 报"功能切换失败"返回;       // 0xF 表示切换未生效
    改分频：高速 50MHz / SDR50 100MHz / SDR104 200MHz / DDR50 50MHz;

【disable_sd_speed_mode 逐行译】
    BLKCNT=1，BLKSIZE=64;
    发 CMD6(arg=0x80FFFFF0) 读 64 字节;     // x=0 = 切回默认
    检查位域 379~376 == 0xF 则报切换失败;
    改分频，card_clk 降到 25MHz 以下;
```

#### 58.5.4.3 查询/使能/禁用 MMC 高速模式

```c
【enable_mmc_high_speed_mode 逐行译】
    发 CMD9 取 CSD;
    检查 SPEC_VER 字段 >= 4;  （<4 报"不支持高速"返回）
    BLKCNT=1，BLKSIZE=512;
    发 CMD8 取 EXT_CSD;
    从 CARD_TYPE 字段看高速是 26MHz 还是 52MHz;
    发 CMD6(arg=0x1B90100);                 // 切高速时序
    发 CMD13 等卡就绪（busy 线释放）;
    再发 CMD8 取 EXT_CSD，确认 HS_TIMING(字节185)==1;
    不是 1 则报"切换失败"返回;
    改分频：按 CARD_TYPE 给 26 或 52MHz;

【disable_mmc_high_speed_mode 逐行译】
    发 CMD6(arg=0x2B90100);                 // 切回普通时序
    BLKCNT=1，BLKSIZE=512; 发 CMD8 取 EXT_CSD 确认 HS_TIMING==0;
    不是 0 报"切换失败"返回; card_clk 降到 20MHz 以下;
```

#### 58.5.4.4 设置 MMC 总线宽度

```c
【change_mmc_bus_width 逐行译】
    发 CMD9 取 CSD，检查 SPEC_VER >= 4;
        （<4 报"不支持多位宽度"返回）
    发 CMD6(arg=0x3B70x00);
        // x=6: 8位DDR  x=5: 4位DDR  x=2: 8位  x=1: 4位  x=0: 1位
    发 CMD13 等卡就绪（busy 释放）;
    （此后数据事务与普通模式相同）
```

> 译注：这一节就是将来提速（第 5 级优化）的现成配方——eMMC 走 .4.3+.4.4
> （CMD6 arg 0x1B90100 切 52MHz + 0x3B700200 切 8 位），TF 卡走 .4.2。

### 58.5.5 ADMA 操作

【原文】Code for ADMA1 Operation and ADMA2 Operation can be found here.
（58.5.5.1/58.5.5.2 以伪代码给出两种描述符的构造方法）

【译文】ADMA1 与 ADMA2 操作的代码如下。（本版 RM 此节仅含伪代码）

```c
【Set_adma1_descriptor 逐行译】
if (要开始数据传输) {
    // 确保地址 4KB 对齐
    设 'Set' 型描述符 { Act=01; [31:12]=数据长度(字节) };
    设 'Tran' 型描述符 { Act=10; [31:12]=地址(4KB 对齐) };
}
else if (要在不连续地址取下个描述符) {
    设 { Act=11; [31:12]=下一描述符地址(4KB 对齐) };   // Link 型
}
else {  // 结束/其它类型
    按相应 Act 位设置;
}

【Set_adma2_descriptor 逐行译】
if (要开始数据传输) {
    // 确保地址 32 位边界（低 2 位恒 '00'）
    描述符高 32 位 = 本次传输起始地址;
    [31:16] = 数据长度(字节);
    Act = '10';
}
else if (要在不连续地址取下个描述符) {
    Act = '11';
    // 地址须 32 位边界（低 2 位置 '00'）
    高 32 位 = 下一描述符地址;
}
else {  // 其它类型按相应 Act 位设置
}
if (是最后一个描述符) 置 'End' 位 = 1;
if (要为本描述符产生中断) 置 'Int' 位 = 1;
```

> 批次 5c-2（58.5.6 快速启动 + 58.4.6.2/.6.3）续于下一批。
