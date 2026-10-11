# justiceBL —— 裸机构建（代码原则见 docs/CODING_STYLE.md）
#
# 目标：
#   make            编译出 build/justiceBL.bin
#   make imx        打包 OCRAM 版 .imx（ROM 可引导）
#   make imx-ddr    打包 DDR 对照版（嵌官方 DCD，正点原子同款配方）
#   make verify     黄金对拍：与官方镜像逐字段断言（改打包器/链接脚本必跑）
#   make dis        反汇编到 build/justiceBL.dis
#   make clean      清理 build/

SDKROOT ?= $(abspath ..)
CROSS   ?= $(SDKROOT)/tools/gcc-11.3-bootlin/bin/arm-buildroot-linux-gnueabihf-

CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump

CFLAGS  := -march=armv7-a -marm -ffreestanding -nostdlib -nostartfiles \
	   -Wall -Wextra -O2 -g -fno-stack-protector -Iinclude -Idrivers
LDFLAGS := -static -nostdlib -Wl,--build-id=none

BUILD  := build
TARGET := justiceBL

# 模块清单：新外设 = drivers/ 新增一对 .c/.h + 此处登记一行
OBJS := $(BUILD)/start.o \
	$(BUILD)/main.o \
	$(BUILD)/drivers/uart.o \
	$(BUILD)/drivers/led.o \
	$(BUILD)/drivers/mmc.o \
	$(BUILD)/lib/crc32.o

UBOOT_IMX ?= $(SDKROOT)/output/uboot/u-boot-dtb.imx

all: $(BUILD)/$(TARGET).bin

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@ls -l $@

$(BUILD)/$(TARGET).elf: $(OBJS) justiceBL.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-T,justiceBL.ld -o $@ $(OBJS)

$(BUILD)/$(TARGET)-ddr.elf: $(OBJS) justiceBL-ddr.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,-T,justiceBL-ddr.ld -o $@ $(OBJS)

$(BUILD)/$(TARGET)-ddr.bin: $(BUILD)/$(TARGET)-ddr.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/dcd.bin: $(UBOOT_IMX)
	@mkdir -p $(BUILD)
	dd if=$< of=$@ bs=1 skip=44 count=488 status=none

imx: $(BUILD)/$(TARGET).bin mkimx.py
	python3 mkimx.py $< $(BUILD)/$(TARGET).imx

imx-ddr: $(BUILD)/$(TARGET)-ddr.bin $(BUILD)/dcd.bin mkimx.py
	python3 mkimx.py $(BUILD)/$(TARGET)-ddr.bin $(BUILD)/$(TARGET)-ddr.imx \
		87800000 $(BUILD)/dcd.bin

verify: imx imx-ddr
	python3 verify_imx.py $(BUILD)/$(TARGET)-ddr.imx $(UBOOT_IMX) \
		$(BUILD)/$(TARGET).imx

dis: $(BUILD)/$(TARGET).elf
	$(OBJDUMP) -d $< > $(BUILD)/$(TARGET).dis

clean:
	rm -rf $(BUILD)

.PHONY: all imx imx-ddr verify dis clean
