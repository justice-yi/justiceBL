# justiceBL 代码原则（对齐 Linux 内核风格）

依据 Linux 内核 `Documentation/process/coding-style.rst` 裁剪，结合裸机项目实际。

## 1. 模块化

- **一个外设一个模块**：`drivers/uart.c` + `drivers/uart.h`、`drivers/led.c` + `drivers/led.h`。
- 对外只暴露 `.h` 里声明的 API；模块内部函数/变量一律 `static`。
- **寄存器知识不上抛**：基址、偏移、位定义是模块私有，调用者只见 `uart_putc()` 这样的语义接口。
- 新增外设 = 新增一对 `.c/.h` + 在 Makefile `OBJS` 登记一行，别处零改动。
- 驱动自带来自己的引脚复用与时钟门控（引脚少于 ~10 个时；多了再抽 `pinctrl` 模块——YAGNI）。

## 2. 命名

- 全小写 + 下划线（kernel 风格），**禁止驼峰**。
- 函数：`<模块>_<动作>` —— `uart_init()` / `uart_putc()` / `led_set()`。
- 全局量：名词，可带计数风格 —— `uart_tx_timeouts`（类比内核 `jiffies`）。
- 宏与常量全大写：`MX6ULL_UART1_BASE`、`UCR2_SRST`、`UART_PAD_CONFIG`。
- 定宽类型 `u8/u16/u32`（`include/types.h`，与内核同款），裸机不用 `int` 谈寄存器。
- 寄存器访问统一 `writel(val, addr)` / `readl(addr)`——与内核**同名同义**。

## 3. 排版（内核规则要点）

- TAB = 8 列缩进；行宽 80。
- 函数左大括号**换行**；`if/for/while` 左大括号**同行**。
- 单语句也**总是加大括号**（比内核略严，防手滑追加语句踩坑）。
- 注释一律 `/* */`；公开函数用 kernel-doc 风格头：

```c
	/**
	 * uart_putc() - 发送一个字符（带超时的轮询）
	 * @c: 待发送字符
	 */
```

- 每个文件头：SPDX 行 + 一段“本文件是什么、寄存器/序列依据是什么”——
  依据必须可回溯（RM 章节、设备树行号、参考代码路径），这是本项目用血换来的规矩。

## 4. 工程化

```
justiceBL/
├── start.S              启动汇编（平台层，几乎不再动）
├── main.c               应用层：自检心跳 / 诊断 / 未来加载 U-Boot
├── include/             公共头（types.h）
├── drivers/             外设驱动 + SoC 寄存器头（imx6ull.h）
├── docs/                规范文档（本文件）
├── justiceBL[|-ddr].ld  链接脚本（OCRAM / DDR 对照实验）
├── mkimx.py             打包器（IVT/BootData/DCD）
├── verify_imx.py        黄金对拍（与官方镜像逐字段断言）
└── Makefile             目标化构建
```

- 构建产物全部进 `build/`，不入库（`.gitignore`）。
- Makefile 目标各司其职：`all` / `imx` / `imx-ddr` / `verify` / `dis` / `clean`。
- **改 mkimx.py 或链接脚本必跑 `make verify`**——黄金对拍是字节序/布局错误的哨兵。
- 版本号进 banner；行为变更记录在 README「当前状态」。
- 文档先行：新模块先在 README 记一笔"是什么、依据什么"，再写代码。

## 5. YAGNI 备忘（现在刻意不做，写明何时做）

- `delay_loops()` 住在 main.c：第 4 级 GPT 定时器到位后升格 `drivers/delay.c`，
  提供 `udelay()/mdelay()` 精确延时。
- 无 malloc、无中断、无 console 框架：分别在第 5/6 级按需引入。
- OCRAM/DDR 双链接脚本并存：DDR 版是调试对照，第 6 级定稿后删。
