# justiceBL —— 自制第一级 bootloader（学习项目）

启动链：ROM → **justiceBL** → U-Boot。目标：把 U-Boot 从 eMMC 1KB 裸区搬进
普通分区，让它变成 fastboot/HTTP 可烧录的日常分区；顺便学 ARM 裸机开发与汇编。

**参考资料**（Windows 侧，WSL 经 /mnt/d，详见 SDK 根 `AGENTS.md`）：
第三方裸机代码 `/mnt/d/Linux开发板/imx6ull_NoosProgramProject`（12_定时器→第4级、
14_TF卡→第5级）；原理图以 `02、开发板原理图/IMX6ULL_ALPHA_V2.2(底板).pdf` 为准；
芯片手册在 `07、I.MX6U参考资料`。

技术路线：i.MX6ULL RM 第 8 章的**标准 IVT 镜像**机制——ROM 不关心 1KB 偏移处的
镜像是什么代码，只认 IVT/BootData/DCD 结构；justiceBL 占据该位置，运行后从
普通分区读出 `u-boot-dtb.bin` 加载到 0x87800000 并跳转。等价于手写一个迷你 SPL。

## 当前状态（v0.4：模块化重构完成）

**代码原则见 `docs/CODING_STYLE.md`**：分模块、kernel 风格命名与排版、工程化构建。

```
justiceBL/
├── start.S              启动汇编（平台层：向量表/SVC/关MMU·cache/栈/清bss）
├── main.c               应用层：自检心跳 + 诊断灯语（调用 drivers 的 API）
├── include/types.h      u8/u16/u32 + BIT（内核同款）
├── drivers/
│   ├── imx6ull.h        SoC 基址 + writel/readl（与内核同名同义）
│   ├── uart.[ch]        UART1 驱动（115200 8N1 轮询，含超时诊断）
│   └── led.[ch]         LED0 驱动（GPIO1_IO03 低亮）
├── docs/CODING_STYLE.md 代码原则
├── justiceBL[|-ddr].ld  链接脚本（OCRAM 正式版 / DDR 对照版）
├── mkimx.py             打包器（IVT/BootData/DCD，指针字段小端！）
├── verify_imx.py        黄金对拍（与官方镜像逐字段断言，改打包器必跑）
└── Makefile             all / imx / imx-ddr / verify / dis / clean
```

功能与 v0.3.2 一致（自检灯语①②④③、超时诊断、115200 banner、心跳），
纯结构重构。调试状态：板上验证进行中（见"上板验证"节灯语协议）。

### 寄存器速查（依据：SDK 设备树/clk 代码，源头是原理图）

| 用途 | 寄存器 | 值/位 |
|---|---|---|
| LED0 点亮 | GPIO1_IO03（核心板，**低电平亮**） | mux=ALT5，GDIR bit3=输出，DR bit3: 0亮/1灭 |
| LED0 引脚 | 0x020E0068 / 0x020E02F4 | mux=5，pad=0x10B0 |
| UART1 引脚 | 0x020E0084/0x020E0088 (mux)，0x020E0310/0x0314 (pad) | mux=ALT0，pad=0x1b0b1 |
| UART1 波特率 | 0x02020090/0xA4/0xA8 | UFCR=(5<<7)，UBIR=71，UBMR=3124 → 115200 整 |
| GPIO1 时钟 | CCM_CCGR1 0x020C406C | bit27:26=11 |
| UART1 时钟 | CCM_CCGR5 0x020C407C | bit25:24=11 |

波特率依赖复位默认时钟树（PLL3=480M → /6 = 80MHz 参考钟）；显式配时钟是第 4 级。

## 上板验证

### 方式 A：TF 卡真启动链（推荐——走 ROM→镜像 全流程，不依赖板上已有软件）

```bash
make imx                                      # 产出 build/justiceBL.imx（IVT+BootData+bin）
dd if=build/justiceBL.imx of=/dev/sdX bs=512 seek=2 conv=notrunc   # 1KB 偏移，sdX 换成 TF 卡！
```

- 打包由 `mkimx.py` 完成：IVT(0xD1) + BootData + 代码，**dcd=NULL**（OCRAM 上电即活，
  这是 OCRAM 方案的红利；将来加载 U-Boot 进 DDR 时才需要抄 DCD 表）。
- ROM 只认结构不认内容：1KB 偏移处必须是 IVT，**裸 .bin 直接 dd 是不会启动的**。
- **boot_data.start 对应卡的 0 字节**（= self − 0x400），ROM 按"设备偏移 D → start+D"搬运
  （正点原子指南 9.4 节 + 官方 u-boot-dtb.imx 头实测；Boot Data 结构只有 12 字节）。
- **IVT 指针字段与 Boot Data 均为小端**（官方镜像字节实测 + mkimage Data Size 对账验证）；
  RM 说的 big-endian 只适用于 IVT/DCD 头部的 tag/length 和 DCD 内部数据。写错字节序
  的症状 = ROM 拒镜像、回落串行下载（设备管理器冒出 15a2:0080 的 HID 设备）。
- 拨码（正点原子表 9.3.2，SD 卡启动）：**开关 1、7 拨 ON，其余 OFF**（1 0 0 0 0 0 1 0）。
- 卡若带 MBR 分区（分区起始 ≥1MiB），1KB~2KB 落在间隙里，FAT 不受影响，可继续当
  fatload 卡用；纯数据卡直接 dd 也行。
- 拨码切到 TF/SD 启动档（组合见正点原子手册丝印/说明），上电即见 banner + LED 心跳。
- 失败排查：ROM 找不到合法 IVT 会自动回落串行下载（RM 8.2.5）——电脑此时插着 USB
  能看到新 HID 设备（VID 0x15A2），这本身就是"没找到镜像"的信号。

### 方式 B：借板上现成 U-Boot（快速迭代，免拔卡）

```
=> dcache off          ← 必须！U-Boot 的 D-cache 里可能压着"没落地"的脏行，
                          不关就 go 的话，CPU 会执行 RAM 里的陈旧字节（缓存一致性活教材）
=> loady 0x00908000    ← Ymodem 发送 build/justiceBL.bin（终端选 Ymodem 发送）
=> go 0x00908000
```

预期：串口打出 `justiceBL v0.2 @ OCRAM 0x00908000` 两行 banner，核心板 LED0 心跳闪，
每圈一行 `alive #n`。闪太快/太慢改 `main.c` 的 `BLINK_DELAY`。
（`go` 从 U-Boot 起跳：U-Boot 的映射是恒等映射，我们的启动代码自己会关 MMU/cache，兼容。）

## 编译

```bash
make          # 产物 build/justiceBL.bin
make dis      # 额外生成反汇编 build/justiceBL.dis（学汇编看这个）
make clean
```

工具链复用 SDK 自带的 gcc 11.3（`tools/gcc-11.3-bootlin`），无需安装。
SDK 不在本目录上一级时：`make SDKROOT=/path/to/imx6ull-sdk`。

## 地址规划

| 项 | 值 | 理由 |
|---|---|---|
| justiceBL 链接/加载地址 | **OCRAM 0x00908000**（上限 0x00918000，64KB） | RM 8.4.1：0x00907000~0x00918000 是 ROM 启动期留给一级镜像的 68KB 空区；与 U-Boot 官方 SPL 的 TEXT_BASE 同基址；之上 0x00918000~ 是 ROM 的 MMU 表/栈，不能占 |
| OCRAM 的额外好处 | DCD 变可选 | OCRAM 上电即活，不依赖 DDR 初始化——DDR 未配好也能跑（将来加载 U-Boot 到 0x87800000 时再依赖 DDR） |

运行方式（将来）：ROM 按 IVT/BootData 把整个镜像拷到 0x00908000 并跳转；
调试期也可走 SDP：`WRITE_FILE` 到 0x00908000 后 `JUMP_ADDRESS`（flashtool 救援模式）。
三处地址（链接脚本 ORIGIN / .imx 打包的 BootData.start / SDP 的 ADDRESS）必须同值。

## 路线图

```
0. 骨架（本目录现状）
1. 点灯          —— 纯汇编写 GPIO 寄存器
2. UART 打印     —— 轮询方式，从此有调试输出
3. 内存 dump     —— 任意地址十六进制打印（兼作寄存器调试器）
4. 时钟与引脚    —— CCM 时钟树阅读 + IOMUX 配 USDHC 引脚
5. 裸机 eMMC 读  —— USDHC CMD0/2/3/7 + CMD17/18 读扇区（最难一级）
6. 加载 U-Boot   —— 从 uboot 分区固定 LBA 读 u-boot-dtb.bin → 0x87800000
7. 跳转          —— cache 收尾后 ldr pc, =0x87800000
8. 打包 .imx     —— IVT(0xD1)+BootData+抄来的 DCD → 接入 layout.csv/mk-image.sh
```
