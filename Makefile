# justiceBL —— 裸机构建（代码原则见 docs/CODING_STYLE.md）
#
# 目标：
#   make            编译出 build/justiceBL.bin
#   make imx        打包 ROM 可引导镜像（OCRAM + 嵌官方 DCD，stage2 需 DDR）
#   make SELFTEST=1 调试版：加入读回自检（生产路径不含）
#   make dis        反汇编到 build/justiceBL.dis
#   make clean      清理 build/

SDKROOT ?= $(abspath ..)
CROSS   ?= $(SDKROOT)/tools/gcc-11.3-bootlin/bin/arm-buildroot-linux-gnueabihf-

CC      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump

CFLAGS  := -march=armv7-a -marm -ffreestanding -nostdlib -nostartfiles \
	   -Wall -Wextra -O2 -g -fno-stack-protector -Iinclude -Idrivers \
	   -DBUILD_HOST=\"$(shell hostname)\" -DBUILD_USER=\"$(shell whoami)\"
# 开发调试：make SELFTEST=1 加入读回自检+二卡探测（生产路径不含）
ifeq ($(SELFTEST),1)
CFLAGS += -DJUSTICEBL_SELFTEST
endif
LDFLAGS := -static -nostdlib -Wl,--build-id=none

BUILD  := build
TARGET := justiceBL

# 模块清单：新外设 = drivers/ 新增一对 .c/.h + 此处登记一行
OBJS := $(BUILD)/start.o \
	$(BUILD)/main.o \
	$(BUILD)/selftest.o \
	$(BUILD)/boot.o \
	$(BUILD)/drivers/uart.o \
	$(BUILD)/drivers/led.o \
	$(BUILD)/drivers/mmc.o \
	$(BUILD)/drivers/bootdev.o \
	$(BUILD)/drivers/board.o \
	$(BUILD)/drivers/bootmode.o \
	$(BUILD)/drivers/timer.o \
	$(BUILD)/drivers/usb/usb_ch9.o \
	$(BUILD)/drivers/usb/usbd_soc.o \
	$(BUILD)/drivers/usb/usbd.o \
	$(BUILD)/drivers/usb/usb_dl.o \
	$(BUILD)/lib/crc32.o \
	$(BUILD)/lib/math.o \
	$(BUILD)/lib/printf.o

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

# banner（main.c 的 __DATE__/__TIME__）是构建指纹——main.c 不改也要重编，
# 否则连续构建时指纹冻结，烧没烧新版一眼无从分辨（2026-11-08 踩坑）
.PHONY: FORCE
FORCE:
$(BUILD)/main.o: FORCE

$(BUILD)/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/dcd.bin: $(UBOOT_IMX)
	@mkdir -p $(BUILD)
	dd if=$< of=$@ bs=1 skip=44 count=488 status=none

imx: $(BUILD)/$(TARGET).bin $(BUILD)/dcd.bin mkimx.py
	python3 mkimx.py $< $(BUILD)/$(TARGET).imx 908000 $(BUILD)/dcd.bin

dis: $(BUILD)/$(TARGET).elf
	$(OBJDUMP) -d $< > $(BUILD)/$(TARGET).dis

clean:
	rm -rf $(BUILD)

.PHONY: all imx dis clean
