# MMC 读码地图（Linux 5.15.31 / U-Boot 2022.04）—— justiceBL 参考

> 用法：本文档是**跟着读代码用的流程目录**。K=kernel 树，U=uboot 树；每步
> `函数(file:line)`；分支只标"→失败:动作"和代码里看不出来的坑。
> 建议阅读顺序：F3(SD 识别) → U1(U-Boot 初始化) → U3(U-Boot 读链) →
> F5(Linux 异步管线) → F4/U2(两边的速度协商对照) → 其余按需查。
> 背景/协议速查在第 1 章，DTS 在第 5 章，justiceBL 落地在第 6 章。

---

# 第一章 速查表（读码时备查）

## 1.1 速度模式

| 模式 | 时钟 | 位宽 | 电压 | tuning | Linux timing(host.h:53) | U-Boot bus_mode / freq(mmc.c:161) |
|---|---|---|---|---|---|---|
| DS-SD | 25M | 1/4 | 3.3 | 否 | LEGACY=0 | MMC_LEGACY/legacy_speed |
| DS-MMC | 26M | 1/4/8 | 3.3 | 否 | LEGACY | MMC_LEGACY/legacy_speed(26M) |
| HS-SD | 50M | 1/4 | 3.3 | 否 | SD_HS=2 | SD_HS/50M |
| HS-MMC | 26/52M | 1/4/8 | 3.3 | 否 | MMC_HS=1 | MMC_HS/26M、MMC_HS_52/52M |
| DDR52 | 52M | 4/8 | 1.8/1.2/3.3 | 否 | MMC_DDR52=8 | MMC_DDR_52/52M |
| SDR12/25/50 | 25/50/100M | 4 | 1.8 | SDR50 要 | =3/4/5 | UHS_SDR12/25/50 |
| SDR104 | 208M | 4 | 1.8 | 是 | =6 | UHS_SDR104/208M |
| DDR50 | 50M | 4 | 1.8 | 是 | =7 | UHS_DDR50/50M |
| HS200 | 200M | 4/8 | 1.8/1.2 | 是 | =9 | MMC_HS_200/200M |
| HS400 | 200M DDR | 8 | 1.8/1.2 | 是 | =10 | MMC_HS_400(_ES)/200M |

本板（6ULL）：ERR010450 限 SDR≤150M/DDR≤45M（K sdhci-esdhc-imx.c:170-175）；
HS50MHz 是免 tuning、免电压切换的最高档。

## 1.2 命令表（两边代码实际用到的）

| CMD | arg | 响应 | 用途 / 代码锚点 |
|---|---|---|---|
| 0 | 0 | 无 | 复位；K mmc_ops.c:139 / U mmc.c:508 |
| 1 | OCR(bit30=HCS) | R3 | eMMC 协商；K mmc_ops.c:176 / U mmc.c:692 |
| 2 | 0 | R2 | CID（大端拼接）；K/U 同名 mmc_all_send_cid |
| 3 | 0(SD)/RCA<<16 | R6/R1 | 定址；K sd_ops.c:238 / U mmc.c:2452 |
| 6 | 见下 | R1+64B(SD) / R1b(eMMC) | SD 切功能；eMMC 写 EXT_CSD |
| 7 | RCA<<16 | R1b | 选中；arg=0 反选 |
| 8 | 0x1AA / 0 | R7 / R1+512B | SD 握手；eMMC 读 EXT_CSD |
| 11 | 0 | R1 | SD 进 1.8V（后停钟 10ms）；K core.c:1193 mmc_set_uhs_voltage |
| 12 | 0 | R1b | 停多块传输 |
| 13 | RCA<<16 | R1 | 状态/busy 轮询；U mmc.c:253 |
| 16 | 512 | R1 | 块长（U-Boot 每次读前发，mmc.c:487） |
| 17/18 | LBA | R1+数据 | 读单/多块（U 多块后补 CMD12） |
| 19/21 | — | R1+数据 | tuning（SD/MMC） |
| 24/25 | LBA | R1+数据 | 写 |
| 52/53 | fn+addr | R5 | SDIO |
| ACMD6 | **2** | R1 | SD 切 4bit（sd.h:74-75） |
| ACMD41 | OCR(bit30/24) | R3 | SD 协商（S18R=bit24） |
| ACMD51 | 0 | R1+8B | SCR |

SD CMD6 arg：mode0 读支持位=0x00FFFFF0；mode1 切功能=`0x80FFFFF0|func`
（K sd_ops.c:317-325）。eMMC CMD6 arg=`0x03<<24|idx<<16|val<<8`。
64B status：[13]=group1 支持位、[16]=group1 当前选择、[9] 驱动强度、[7:6] 电流。
EXT_CSD 关键 idx：185 HS_TIMING、183 BUS_WIDTH、196 CARD_TYPE、
212-215 SEC_CNT、248 GEN_CMD6_TIME（K mmc.h:243-341）。

## 1.3 响应类型

| 类型 | 特征 | 坑 |
|---|---|---|
| R1 | 32 位状态+CRC7+命令号回显 | SWITCH_ERROR=bit7、RDY=bit8、STATE=bits12:9(TRAN=4) |
| R1b | R1 + DAT0 拉低示忙 | busy 不在响应里，在数据线 |
| R2 | 136 位 CID/CSD | **大端**，读 4 字移位拼接 |
| R3 | 裸 OCR | 无 CRC 无回显，开校验位永远报错 |
| R6 | RCA+状态 | [31:16]=RCA |
| R7 | 回显 | [7:0]必须==0xAA |

---

# 第二章 Linux 主干流程（F1-F7）

## F1 host probe：从设备匹配到可检测卡

```
platform 匹配 fsl,imx6ull-usdhc（imx.c:343 → usdhc_imx6ull_data :277）
sdhci_esdhc_imx_probe (imx.c:1568)
├─ sdhci_pltfm_init (:1576) → sdhci_alloc_host (sdhci.c:3934)
│   └─ mmc_alloc_host (host.c:522) + INIT_DELAYED_WORK(detect, mmc_rescan)(:567)
├─ 驱动预置 caps：1_8V/3_3V_DDR、CD_WAKE (imx.c:1624-1629)
├─ sdhci_esdhc_imx_probe_dt (:1681)          ← DT 解析，两条线：
│   ├─ fsl 私有：tuning-step/start-tap、no-1-8-v→quirk (:1525-1546)
│   └─ mmc_of_parse (host.c:276)              ← 通用属性→caps/f_max
├─ sdhci_add_host (:1687) → sdhci_setup_host (sdhci.c:4138)
│   └─ sdhci_read_caps (:4174)：RESET_ALL → 读 0x40/0x44 硬件 caps
│       → 翻译成 mmc->caps（HISPD→HIGHSPEED、caps1 UHS 连锁置位，见 1.4 节注）
└─ __sdhci_add_host (sdhci.c:4707)
    ├─ request_threaded_irq(sdhci_irq) (:4732)
    ├─ mmc_add_host → mmc_start_host (core.c:2252)
    │   └─ mmc_gpiod_request_cd_irq (slot-gpio.c:88)  ← CD 双沿中断
    └─ sdhci_enable_card_detection (:4758)
```

caps 合成规则（读码时记三条）：硬件 caps 为基底；`sdhci-caps-mask/caps` DT 覆写；
`mmc_of_parse` 的位只增不减；`no-1-8-v` 是唯一常规减法（sdhci.c:4447-4460）。

## F2 插卡 → rescan → attach

```
插卡: mmc_gpio_cd_irqt (slot-gpio.c:30)
 → mmc_detect_change (core.c:1425) → 调度 host->detect work（200ms 消抖）
 → mmc_rescan (core.c:2188)
    ├─ bus_ops->detect 复验已有卡 (:2211)      ← 无 claim 调用，内部自己抢锁
    ├─ get_cd()==0 → power off (:2220-2225)
    └─ for freq in {400k,300k,200k,100k} (core.c:54)
        → mmc_rescan_try_freq (core.c:2035)
            ├─ mmc_power_up (:2042)
            ├─ sdio_reset (CMD52) (:2057)       ← SD/eMMC 忽略
            ├─ CMD0 mmc_go_idle (:2059, mmc_ops.c:139)
            ├─ CMD8 mmc_send_if_cond (:2062)
            └─ 依次试 mmc_attach_sdio(:2070)/sd(:2074)/mmc(:2078)
                └─ 一种失败自动试下一种
```

## F3 SD 识别状态机（核心中的核心，mmc_sd_init_card sd.c:1389）

```
mmc_attach_sd (sd.c:1810)
├─ ACMD41(ocr=0 探测) (:1817)  →失败: return（不是 SD）
├─ mmc_attach_bus(&mmc_sd_ops) (:1821)
├─ mmc_select_voltage (:1842)  →空窗: -EINVAL
└─ mmc_sd_init_card (:1389)   [全程持 claim]
   ① CMD0  mmc_go_idle (sd.c:827→mmc_ops.c:139)
   ② CMD8  mmc_send_if_cond (sd.c:835→sd_ops.c:162)
      arg=VHS(0x1)<<8|0xAA；回显≠0xAA →吞错，按 SD1.x 老卡走（:835-837 只是不加 CCS）
   ③ ACMD41 mmc_send_app_op_cond (sd.c:855→sd_ops.c:118)
      arg=电压窗&0xFF8000 | SD2.0加CCS(bit30) | UHS加S18R(bit24) | >150mA加XPC(bit28)
      100 次×10ms（sd_ops.c:130,150）；R3 bit31=busy
      S18A(bit24)应答 → mmc_set_uhs_voltage (core.c:1193)：CMD11 + 停钟 10ms
        →失败 -EAGAIN: retries--（初值 10, sd.c:812）重来；retries 尽则去 S18R
   ④ CMD2  mmc_all_send_cid (sd.c:877)
      →重识别时 CID 与 oldcard 不符: -ENOENT (:1405-1409)
   ⑤ CMD3  mmc_send_relative_addr (sd.c:1435→sd_ops.c:238)  R6[31:16]=RCA
      →失败: goto free_card
   ⑥ CMD9  mmc_send_csd + mmc_decode_csd (sd.c:1441)
   ⑦ CMD7  mmc_select_card (sd.c:1459→mmc_ops.c:108)  R1b 进 tran 态
   ⑧ ACMD51 mmc_app_send_scr (sd.c:928→sd_ops.c:256)  8B 数据
      解析 mmc_decode_scr (sd.c:201)：SD_SPEC=bits[59:56]、bus_widths=bits[51:48]、
      spec3=bit47；bus_widths 缺 1bit 或 4bit → -EINVAL (:239-243)
   ⑨ CMD6 mode0 mmc_read_switch (sd.c:949→316)  arg=0x00FFFFF0
      前置跳过：SD1.0 (:321) / CSD 无 SWITCH 类 (:324)
      status[13]&0x02 → hs_max_dtr=50MHz (:355)；
      spec3 卡另存 sd3_bus_mode=[13]、drv_type=[9]、curr_limit=[7]|[6]<<8 (:358-363)
   ⑩ 速度协商（→F4）
   ⑪ ACMD6 mmc_app_set_bus_width (sd.c:1523→sd_ops.c:97)  arg=2
      成功 → mmc_set_bus_width(4)
   ⑫ mmc_add_card (bus.c:308)  →匹配 mmc_blk driver →F5 注册链
   free_card 标签 (:1566)：mmc_remove_card + return err
```

## F4 速度协商（sd.c）

```
非 UHS（HS）路径:
  mmc_sd_switch_hs (sd.c:374)
  ├─ 前置：host 有 MMC_CAP_SD_HIGHSPEED 且卡 hs_max_dtr≠0 (:385-388)
  ├─ CMD6 mode1 arg=0x80FFFFF1 (group1 选 HS)
  └─ 回读 status[16]&0xF==1 才成功 (:399-405) → mmc_set_timing(SD_HS)

UHS 路径（rocr 带 S18A 且 host UHS）: mmc_sd_init_uhs_card (sd.c:614)
  ① sd_update_bus_speed_mode (:444)
     host 无任何 MMC_CAP_UHS_* → 锁死 DS (:450-453)
     否则按 SDR104>DDR50>SDR50>SDR25>SDR12 取 host∩卡 (:455-474)
  ② 驱动强度 group2: mmc_sd_switch(1,2,drv) 校验 status[15]&0xF (:427)
  ③ 电流 group3: 200/400/600/800mA 档 (:597)
  ④ 速度 group1: sd_set_bus_speed_mode (:477) CMD6 mode1 + 校验 [16] (:507-511)
     → mmc_set_timing + mmc_set_clock(uhs_max_dtr) (:515)
  ⑤ SDR50/104 → mmc_execute_tuning (sd.c:662→core.c:919)
```

eMMC 版入口：`mmc_select_bus_width`（mmc.c:998，8→4 顺序，EXT_CSD[183]）+
`mmc_select_timing`（mmc.c:1511，HS→HS200→HS400）；所有 CMD6 经 `__mmc_switch`
（mmc_ops.c:549）：arg=`0x03<<24|idx<<16|val<<8`，busy 三级——
硬件 WAIT_WHILE_BUSY → `__mmc_poll_for_busy`(:473, 优先 ops->card_busy 读 DAT0,
否则 CMD13; 间隔 32us 指数退避到 32ms) → mdelay 硬等；SWITCH_ERROR(bit7)→-EBADMSG。

## F5 块读请求异步管线（block.c + queue.c + sdhci.c）

```
注册（卡 add 后一次）:
  mmc_blk_probe (block.c:2885) → mmc_blk_alloc (:2470)
  → mmc_init_queue (queue.c:409): blk_mq_alloc_tag_set(ops=mmc_mq_ops :339)
  → device_add_disk (block.c:2445)
  CMD23 判定: host CAP_CMD23 && 卡 SCR CMD23 → MMC_BLK_CMD23 (:2424-2425)

单次读:
  blk-mq → mmc_mq_queue_rq (queue.c:224)
  ├─ mmc_get_card (claim+runtimePM, :287)
  ├─ →BLK_STS_RESOURCE: 正在恢复或卡 busy (:248-251)
  └─ mmc_blk_mq_issue_rq (block.c:2265)
      └─ mmc_blk_rw_rq_prep (:1575)  组包:
          单块 CMD17 / 多块 CMD18 (:1602-1607)
          有 CMD23 → sbc 预置块数 (:1629-1638)；否则 mrq.stop=CMD12
      └─ mmc_start_request (block.c:2243→core.c:336→__mmc_start_request :217)
          └─ host->ops->request = sdhci_request (sdhci.c:2149)  ← 交出即返回
              ├─ 先发 sbc（如有, :2167）
              └─ sdhci_send_command (:1605)  寄存器级:
                  ① 查 PRESENT_STATE CMD/DAT_INHIBIT (:1620-1630)
                  ② sdhci_prepare_data (:1090): DMA map、ADMA 建表写 0x58、
                     BLOCK_SIZE/COUNT
                  ③ 写 ARGUMENT → TRANSFER_MODE(:1426-1465:
                     DMA/BLKCNTEN/AUTOCMD23/READ/MULTI 位) → COMMAND 写即发射
中断完成:
  sdhci_irq (sdhci.c:3436)
  ├─ INT_RESPONSE → sdhci_finish_command (:1769): 读 RESPONSE、R2 移位
  ├─ DATA_CRC/DTOE → data->error → finish_data (:3362)
  ├─ INT_DMA_END → SDMA 推进到下一个 512K 边界 (:3394-3409)
  ├─ DATA_AVAIL/SPACE_AVAIL → PIO 逐字搬 0x20 (:580-616)
  └─ __sdhci_finish_mrq → queue_work(complete_work) (:1522)   ← 硬中断持锁只能排队
  sdhci_complete_work (:3127) → sdhci_request_done (:3002)
  → mmc_request_done (core.c:139) → mmc_blk_mq_req_done → 回块层
```

## F6 错误恢复（mmc_blk_mq_rw_recovery block.c:1785-1873）

```
进入: mmc_blk_rq_error (:1875)：sbc/cmd/stop/data error 或 resp&CMD_ERRORS(:1241)
步骤（顺序固定）:
  ① CMD13 查状态；坏 → bytes_xfered=0
  ② mmc_retune_release (:1804)           ← 下个请求触发 retune
  ③ 卡已拔 → return
  ④ mmc_blk_fix_state (:1657)：CMD12(retries=5) 拉回 tran 态
  ⑤ SD 写: ACMD22 问卡实写块数修正 bytes_xfered (:1829)
  ⑥ mmc_blk_reset (:987)：hw_reset / power_cycle + 分区复位
  ⑦ bytes_xfered>0 → requeue 续传剩余
  ⑧ 重试矩阵：总 5 / data 错 2 / 多块读失败降级 mmc_blk_read_single 逐扇区(:1675)
超时路径: mmc_mq_timed_out (queue.c:119) → 恢复流程；块层超时 600s (:279)
```

## F7 set_ios 配置下发链（core → 寄存器）

```
mmc_set_clock(core.c:910)/set_bus_width(:965)/set_timing(:1261)/set_signal_voltage
 → 改 host->ios.* → mmc_set_ios (core.c:884) → ops->set_ios
 = sdhci_set_ios (sdhci.c:2260)
    ├─ ops->set_clock = esdhc_pltfm_set_clock (imx.c:875)
    │   清 FRC_SDCLK_ON→等 CLOCK_GATE_OFF(:887) → ERR010450 限频 SDR150M/DDR45M(:918)
    │   → pre_div(2^n)×div(1-16) → 写 SYS_CTRL: (div-1)<<4|(pre_div>>1)<<8 (:926-944)
    │   → 等 PRSSTAT bit3 稳定 → 复开 FRC_SDCLK_ON
    ├─ ops->set_bus_width → HOST_CONTROL 位宽位（经 hook 落 bits2:1，4bit=01）
    ├─ HS/UHS → HISPD 位 (:2318)；ops->set_uhs_signaling = esdhc_set_uhs_signaling
    │   (imx.c:1221)：DDR 写 MIX_CTRL DDREN；HS400 写 HS400_EN；尾接 pinctrl 切档
    │   (state_100mhz/200mhz, imx.c:1101)
    └─ 1.8V → HOST_CONTROL2.VDD_180 (:2550) → hook 翻译成 VENDORSPEC.VSELECT
```

注意：uSDHC 寄存器窗口与 SDHCI 标准不同，全部经 imx 的 read_l/write_l hook 翻译
（imx.c:1315-1322 注册；映射表：TRANSFER_MODE 低 16↔MIX_CTRL(0x48) 低 8 位、
COMMAND↔0x0C 高 16、HOST_CONTROL2↔VENDORSPEC+0x3C 高 16、CLOCK_CONTROL↔
VENDORSPEC.FRC_SDCLK_ON）。tuning：HOST_CONTROL2.EXEC_TUNING→hook→0x3C bit22+
MIX_CTRL bit24（硬件自动步进 tap，软件只查 TUNED_CLK，sdhci.c:2796-2801）。

---

# 第三章 U-Boot 主干流程（U1-U4）

## U1 probe → 两段式 init → startup

```
DM probe:
  fsl_esdhc_bind (:1701) → mmc_bind (mmc-uclass.c:387)
      创建 mmc_blk 子设备 (blk_create_devicef :399, ops=mmc_bread 等 :486)
  fsl_esdhc_of_to_plat (:1374)：reg/tuning 参数/cd、wp-gpios/vqmmc
  fsl_esdhc_probe (:1456) → fsl_esdhc_init (:1130)
      复位、读硬件 caps（HS/HS52 位 :1202；UHS/HS200 需 CONFIG+flags :1241-1257）
      → mmc_of_parse (mmc-uclass.c:206)  DT→cfg->host_caps
  mmc_blk_probe (mmc-uclass.c:457) → mmc_init (mmc.c:2934)

board_r: initr_mmc (board_r.c:398) → mmc_initialize (mmc.c:3047)
  → mmc_probe (:3009) → mmc_do_preinit (mmc-uclass.c:331, 提前 start_init)

枚举:
  mmc_init (:2934) → mmc_start_init (:2860) + mmc_complete_init (:2917)
  mmc_get_op_cond (:2774):
    power_cycle  →失败: UHS 禁用（host_caps &= ~UHS_CAPS，:2792-2803）
    mmc_reinit（host 复位+set_sysctl 400k, fsl:1028）
    mmc_set_initial_state (:2707: 3.3V/LEGACY/1bit/clock=0)
    CMD0 (:508) → CMD8 (:2646) → sd_send_op_cond (:591)
      1000 次×1ms；arg=电压窗&0xff8000|HCS|S18R
      S18A=(resp&0x41000000)==0x41000000 → mmc_switch_voltage (:530)
      →失败且 uhs: power_cycle 重试；-ETIMEDOUT → mmc_send_op_cond (:692, eMMC)
  mmc_complete_init → mmc_startup (:2416):
    CMD2(:2436) → CMD3(:2452) → CMD9(:2467)+CSD 解析(:2481)
    → CMD7(:2557) → [eMMC: startup_v4 (:2210) CMD8 读 EXT_CSD 512B]
    → 速度/位宽协商（→U2）→ 填 blk_desc (:2613)
```

## U2 模式协商（mmc.c:1647-2203）

```
SD: sd_select_mode_and_width (:1733)
  caps = card_caps & host_caps (:1763)；!uhs_en → caps &= ~UHS_CAPS (:1765)
  for 模式 in sd_modes_by_pref[:1689]（SDR104→SDR50→DDR50→SDR25→SD_HS→SDR12→LEGACY）
    for 位宽 in [4bit, 1bit]
      sd_select_bus_width (:1436, ACMD6)
      sd_set_card_speed (:1387, CMD6)
      mmc_select_mode (:187: 记 selected_mode+tran_speed+ddr_mode)
      mmc_set_clock (:1597, 钳 f_min/f_max)
      [SDR104: tuning]
  全败 → LEGACY (:1814)
eMMC: mmc_select_mode_and_width (:2067)
  每轮: set_lowest_voltage → CMD6 BUS_WIDTH → [HS400 序列 :1968] / CMD6 HS_TIMING
  → mmc_read_and_compare_ext_csd (:1832)   ← 每升一档重读 EXT_CSD 比对验证
下发: mmc_set_clock/bus_width → mmc_set_ios → dm_mmc_set_ios (mmc-uclass.c:58)
  → esdhc_set_ios (fsl:1598) → esdhc_set_ios_common (:914)
      esdhc_set_timing (:714, MIX_CTRL+pinctrl 切档) → set_sysctl (:593)
      → 位宽 PROCTL_DTW (:977)
```

## U3 块读全链（命令 → 硬件）

```
mmc read: do_mmc (cmd/mmc.c:1066) → do_mmc_read (:346)
 → blk_dread (blk-uclass.c:439)
     ├─ blkcache_read 命中即返回 (:449)
     └─ ops->read = mmc_bread (mmc.c:450)
         ├─ hwpart 切换 blk_dselect_hwpart(:474) → mmc_switch_part(:1020, CMD6 PART_CONF)
         ├─ 越界检查 → CMD16 设块长 (:487)
         └─ 按 b_max 分批 → mmc_read_blocks (:398)
             >1 块 CMD18 + CMD12(R1b) 收尾；单块 CMD17
             → mmc_send_cmd (mmc-uclass.c:53) → dm_mmc_send_cmd (:36)
             → fsl_esdhc_send_cmd (:1589) → esdhc_send_cmd_common (:419):
                 ① irqstat=-1 清历史+sync
                 ② 等 bus 空闲: PRSSTAT CICHB/CIDHB/DLA
                 ③ esdhc_setup_data (:322): BLK_ATT / DMA map+表
                 ④ irqsigen=0（纯状态轮询）
                 ⑤ 写 CMDARG；MIX_CTRL=(old&0xFFFFFF80)|(xfertyp&0x7F)|(ddr?DDREN:0)
                    XFERTYP=xfertyp&0xFFFF0000      ← 低 7 位 mode 位进 MIX_CTRL
                 ⑥ 等 IRQSTAT CC（1000ms 超时）；CTOE/CMD_ERR → 超时/-ECOMM
                 ⑦ 无 data 且 R1b → 轮询 PRSSTAT DAT0 变高（5000ms）
                 ⑧ R2: ((rsp3<<8)|(rsp2>>24))... 拼接；否则读 RSP0
                 ⑨ data: PIO=esdhc_pio_read_write (:223, BRR 轮询逐字+大端 swap)
                    DMA=等 TC + dma_unmap 防推测取污染
fatload: do_load → fs_read (fs/fs.c:557) → fat_read_file → disk_read
  → blk_dread →（同上）
写: mmc_bwrite (mmc_write.c:188) → mmc_write_blocks (:130, CMD25+CMD12)
```

## U4 SPL 路径

```
common/spl/spl_mmc.c: spl_mmc_find_device (:164)
 → mmc_init_device (mmc.c:3073, 单设备 probe+preinit)
 → mmc_load_image_raw_sector (:91): blk_dread 读 IVT 头再整块读
 —— 复用与完整 U-Boot 同一套 mmc.c；MMC_TINY 下固定 LEGACY/1bit (:2583-2586)
```

---

# 第四章 次要流程索引（一行一个，入口函数即 grep 锚点）

| 流程 | 入口 (file:line) | 一句话 |
|---|---|---|
| 卡拔出 | mmc_sd_detect (sd.c:1597)/mmc_detect (mmc.c:2017) | get_card alive 检查→claim→detach_bus→power_off |
| host 移除 | mmc_remove_host (host.c:630) | stop_host→free |
| suspend | mmc_bus_suspend (bus.c:170) | 先冻结块层 IO 再 _mmc_suspend（flush cache→sleep/deselect→power_off, mmc.c:2061） |
| resume | _mmc_resume (mmc.c:2114) | power_up→mmc_init_card(oldcard) |
| runtime PM | __mmc_claim_host (core.c:780) | 首次 claim get_sync、释放 put_autosuspend |
| tuning 细节 | sdhci_start_tuning (sdhci.c:2654) | BLOCK_SIZE=64、仅开 DATA_AVAIL、40 次上限；imx 翻译见 F7 注 |
| SDIO 识别 | mmc_attach_sdio (sdio.c:1184) | CMD5→CCCR→CIS→function 枚举 |
| ioctl | mmc_blk_ioctl (block.c:759) | blk_execute_rq 派进队列，RPMB 前置 CMD23 |
| CQE 恢复 | mmc_cqe_recovery (core.c:540) | CMD12(R1B,1s)→retune |
| U-Boot hwpart | mmc_switch_part (mmc.c:1020) | CMD6 写 PART_CONF |
| U-Boot CD | dm_mmc_get_cd → fsl:esdhc_get_cd~ | probe 一次性，检测不到跳枚举 |
| blkcache | blkcache_read/write (blk-uclass.c:439+) | 读透明缓存，写失效 |

---

# 第五章 DTS（读 dts/dtsi 时对照）

## 5.1 属性 → 代码效果（Linux，解析 mmc_of_parse host.c:276-435）

| 属性 | 效果 |
|---|---|
| bus-width=1/4/8 | caps 4/8BIT（:286-305，8 fallthrough 兼 4） |
| max-frequency | f_max（:308），set_clock 钳位（core.c:914） |
| cd-gpios/cd-inverted/cd-debounce-delay-ms | GPIO 检测/极性/消抖（:327-332） |
| non-removable / broken-cd | 跳 CD / NEEDS_POLL（:324/:334） |
| cap-sd-highspeed / cap-mmc-highspeed | caps 位（:359-362） |
| sd-uhs-sdr12/25/50/104/ddr50 | MMC_CAP_UHS_*（:363-372） |
| mmc-hs200-1_8v 等 | caps2（:388-403） |
| no-1-8-v | 例外：sdhci 层 quirk（imx.c:1542）→清 UHS/HS200 |
| no-sdio/no-sd/no-mmc | 识别跳过该类型（:404-409） |
| sdhci-caps-mask/caps | 硬件 caps 减/加（sdhci.c:4044-4056） |
| vmmc/vqmmc-supply、mmc-pwrseq | 供电/IO 电压/上电时序 |
| fsl,tuning-step/start-tap | CMD19 调参（imx.c:1536-1538） |
| pinctrl state_100mhz/200mhz | 高速档引脚电气切换（imx.c:1550） |

## 5.2 本板（imx6ull-alientek-emmc.dts）

- `&usdhc1`（:935，TF）：三档 pinctrl（pad 0x17059/0x170b9/0x170f9）、cd-gpios、
  vmmc-supply；bus-width=4 继承自 imx6ul.dtsi:937。**速度类属性 0 个** →
  caps 无 UHS → 不做 CMD11 → HS 50MHz 4bit 3.3V。
- `&usdhc2`（:947，eMMC）：bus-width=8、non-removable。**没写 mmc-hs200-1_8v** →
  caps2 无 HS200 → mmc_select_timing 只能选 HS → 52MHz 8bit SDR。
- 规则：**DT caps 是允许清单；最终速度 = 硬件 caps ∩ 驱动 flags ∩ DT ∩ 卡能力**。

U-Boot 侧（fsl_esdhc_of_to_plat I:1374-1454 + mmc_of_parse mmc-uclass.c:206-274）
只读 reg/tuning/cd/wp/vqmmc/bus-width/max-frequency/cap-*/no-1-8-v；
忽略全部 PM 类属性；速度上限=min(per 时钟,200MHz)（fsl:1207-1208）；
evk 未开 UHS/HS200 CONFIG、6ul 匹配条目 flags=0（fsl:1684-1697）→ 同样 HS 档。

---

# 第六章 justiceBL 落地

## 6.1 映射

| justiceBL | U-Boot | Linux |
|---|---|---|
| block_source 接口 | BLK uclass | 块层 |
| mmc.c 识别/提速 | mmc.c 状态机 | sd.c/mmc.c/ops |
| usdhc_* 寄存器层 | fsl_esdhc_imx.c | sdhci.c+esdhc-imx |
| 逐块 CMD17 读 | mmc_read_blocks(CMD18) | mmc_blk_rw_rq_prep |

## 6.2 四项改造（按性价比）

1. **模式表**（学 U2 的 sd_modes_by_pref）：`{mode, caps, hz}[]` 优先级表 +
   caps 位图交集，试错降级；加新模式只加表项。
2. **提速后自检**（学 mmc_read_and_compare_ext_csd :1832）：speed_up 尾部读
   LBA2 校验 IVT 头（0xD1），失败降回 DS 重试一次，再败报停。
3. **set_ios 单点**（学 F7）：时钟/位宽收敛成一个 `usdhc_set_ios(base, ios)`，
   hw_init 与 speed_up 共用；将来 pinctrl 档挂这里。
4. **CSD 解析**（学 mmc_decode_csd）：v2 卡 `容量=(C_SIZE+1)<<19`，v1 走
   READ_BL_LEN×C_SIZE；容量进 struct mmc_card，stage2 长度用真实容量校验。

不做（YAGNI）：异步/中断、三总线多态、regulator/PM、错误恢复框架、ADMA。
差距清单：CMD18 多块读、写路径、SSR（永不需要）、tuning（不做 UHS 不需要）。
